#pragma once
#include <hlclient/client/temporary_effect.hpp>
#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/game_api/surface_decals.hpp>
#include <array>
#include <optional>
#include <string_view>

namespace hlclient::game_api {
// Borrowed advertised resource token, not file-open permission. Materialization
// still resolves only approved session assets. Modules retain bounded slots.
struct EffectResourceBinding {
    enum class Kind : std::uint8_t { model, sound, decal };
    Kind kind{};
    std::uint16_t slot{};
    std::string_view name;
};
// Atomic committed dynamic server sound evidence, independent of audio output.
// Host converts the monotonic receive clock once. No PCM/device or asset IO.
struct CommittedSoundObservation {
    std::uint64_t generation{}, record{}, record_ordinal{};
    std::uint32_t message_bit_offset{}, entry_ordinal{}, entity{};
    std::uint16_t sample_slot{}, flags{};
    std::uint8_t channel{};
    assets::AssetVector3 origin{};
    double received_at_seconds{};
};
// Exact committed provenance, not position proximity or player/weapon life.
struct TemporaryEffectIdentity final {
    std::uint64_t generation{}, record{};
    std::uint32_t message_bit_offset{}, entry_ordinal{};
    enum class Source : std::uint8_t { server_record, game_action, server_sound };
    Source source{Source::server_record};
    friend bool operator==(const TemporaryEffectIdentity&, const TemporaryEffectIdentity&) = default;
};
// Owning virtual model token, not a native path, handle, or network entity.
// Resolution uses only already approved session model bindings. Immutable
// transform, sequence zero; no physics, owner following, or damage callbacks.
struct TemporaryStudioIntent final {
    std::array<char,64> model_name{};
    assets::AssetVector3 angles_degrees{};
    double lifetime_seconds{5.0};
};
enum class TemporarySpriteBlend : std::uint8_t { opaque, additive, alpha_blended };
enum class TemporarySpriteAnimation : std::uint8_t { elapsed, fixed };
struct TemporarySpriteIntent final {
    std::uint16_t model_slot{};
    float scale{}, vertical_speed_units{}, opacity{1.0F};
    std::uint8_t frames_per_second{};
    TemporarySpriteBlend blend{TemporarySpriteBlend::additive};
    double maximum_lifetime_seconds{4.0};
    // Neutral modulation, interpreted by the selected module, not the engine.
    std::array<float,3> tint{1,1,1};
    // Neutral bounded ballistic presentation, not gameplay collision/physics.
    // Alpha blending and animation fading are independent material policies.
    float gravity_units{};
    bool fade_with_animation{true};
    // A fixed imported frame remains unchanged throughout its presentation
    // lifetime. Selection is bounded by the imported frame count, not a game
    // resource-specific assumption about its number or role.
    TemporarySpriteAnimation animation{TemporarySpriteAnimation::elapsed};
    std::uint32_t frame_choice{};
};
// One compact request expands once into independent imported sprite instances.
// The supplied deterministic seed selects velocities, lifetimes and frames;
// render/update cadence never consumes random state. No collision or gameplay
// callback is implied by this presentation-only ballistic profile.
struct TemporarySpriteBurstIntent final {
    TemporarySpriteIntent sprite;
    std::uint8_t count{};
    assets::AssetVector3 minimum_velocity{},maximum_velocity{};
    double minimum_lifetime_seconds{},maximum_lifetime_seconds{};
    std::uint32_t seed{};
};
struct TemporaryLightIntent final {
    float radius_units{}, intensity{};
    std::array<float,3> color{};
    double lifetime_seconds{};
};
// An owning approved virtual sample reference and neutral spatial one-shot
// parameters. The committed effect provenance owns delivery, not a weapon or
// player life. Sample readiness/output are application mechanisms.
struct TemporarySoundIntent final {
    LocalSoundReference reference;
    float volume{1.0F}, pitch{1.0F}, attenuation{0.8F};
};
struct TemporaryEffectIntent final {
    TemporaryEffectIdentity identity;
    assets::AssetVector3 origin{};
    double starts_at_seconds{};
    std::optional<TemporarySpriteIntent> sprite;
    std::optional<TemporaryLightIntent> light;
    std::optional<ImpactParticleProfile> particles;
    std::optional<TemporarySoundIntent> sound;
    // Committed order is distinct from the opaque exact record identity.
    // Audio's bounded stale floor must not order arbitrary capture IDs.
    std::uint64_t record_ordinal{};
    std::optional<TemporaryStudioIntent> model;
    // A second component of the same committed effect identity (e.g. the
    // second TE_BLOODSPRITE model). It does not create another causal event.
    std::optional<TemporarySpriteIntent> secondary_sprite;
    // Opaque bounded diagnostic group selected by the module. It never changes
    // rendering, lifetime, resource permission or gameplay execution.
    std::uint8_t diagnostic_tag{};
    std::optional<TemporarySpriteBurstIntent> sprite_burst;
    std::optional<TemporarySurfaceDecalIntent> surface_decal;
};
struct TemporaryEffectStatistics final {
    std::uint64_t received{}, accepted{}, duplicate{}, invalid{}, stale{}, capacity{},
        unsupported_flags{}, deferred_kind{}, explosion{}, smoke{}, main_audio_evidence_pending{},
        main_audio_requested{}, main_audio_suppressed{}, sparks{}, stuck_models{}, blood_sprites{},
        unsupported_palette{};
    std::uint64_t blood_received{},blood_duplicate{},blood_invalid{},blood_stale{},
        blood_palette{},blood_capacity{},blood_delivery_stale{};
    std::uint64_t pop_sound_received{},pop_sound_accepted{},pop_entity_rejected{},
        pop_duplicate{},pop_te_correlated{},pop_resource_missing{},pop_invalid{},pop_capacity{};
    std::uint64_t blood_palette_unavailable{},pop_policy_suppressed{};
    std::uint64_t decal_received{},decal_accepted{},decal_index_unresolved{},
        decal_asset_unsupported{},decal_target_invalid{},decal_capacity{},decal_stale{};
};
struct TemporaryEffectsBatch final {
    std::array<TemporaryEffectIntent,32> effects{};
    std::size_t count{};
    TemporaryEffectStatistics statistics;
};
} // namespace hlclient::game_api

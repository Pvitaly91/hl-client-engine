#pragma once
#include <hlclient/game_api/temporary_effects.hpp>
#include <hlclient/game_api/effect_palette.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <span>
namespace hlclient::games::halflife {
// Half-Life flag interpretation and local-compatible numeric effect profiles.
// No filesystem, scene, damage, player life or projectile ownership.
class TemporaryEffects final {
public:
    void reset(std::uint64_t generation) noexcept;
    void commit(const client::CommittedTemporaryEffect&) noexcept;
    void bind_resource(const game_api::EffectResourceBinding&) noexcept;
    void bind_palette(const game_api::EffectPaletteMetadata&) noexcept;
    void configure_policy(const game_api::EffectPresentationPolicy& policy) noexcept {blood_enabled_=policy.server_blood_sprites_enabled;}
    void entities(std::span<const client::RuntimePacketEntityObservation>,std::uint32_t,std::uint64_t) noexcept;
    void sound(const game_api::CommittedSoundObservation&) noexcept;
    void sniper_world_hit(const game_api::LocalWeaponActionIdentity&,assets::AssetVector3 point,
        assets::AssetVector3 direction,double now) noexcept;
    [[nodiscard]] game_api::TemporaryEffectsBatch drain(double now) noexcept;
    // Immutable bounded HL profile references, available on every startup /
    // readiness poll; no file I/O or one-time drain can lose preparation.
    [[nodiscard]] std::span<const game_api::LocalSoundReference> sound_preparation() const noexcept;
private:
    std::uint64_t generation_{};
    std::array<std::optional<game_api::TemporaryEffectIdentity>,256> seen_{};
    std::size_t next_seen_{};
    game_api::TemporaryEffectsBatch pending_;
    std::array<std::optional<game_api::LocalWeaponActionIdentity>,64> seen_hits_{};
    std::size_t next_hit_{};
    std::uint64_t hit_serial_{};
    std::uint16_t snark_model_{},pop_sample_{},spray_model_{},blood_model_{};
    // Actual advertised decal index -> compiled asset recipe. Neither a SDK
    // enumeration nor a WAD directory ordinal grants a mapping.
    std::array<std::int8_t,4096> decal_assets_by_index_{};
    game_api::EffectPaletteMetadata palette_;
    std::optional<bool> blood_enabled_;
    struct SnarkSource {
        std::uint32_t entity{};
        std::uint64_t born{},retired{};
        bool popped{};
    };
    // Observed continuity only, not server-private allocation generations.
    std::array<SnarkSource,128> snarks_{};
    std::uint64_t entity_ordinal_{};
    struct PopReceipt {
        assets::AssetVector3 origin{};
        double time{};
        bool used{},sound{},matched{};
    };
    std::array<PopReceipt,64> pops_{};
    std::size_t next_pop_{};
    bool correlate_pop(assets::AssetVector3,double,bool) noexcept;
};
}

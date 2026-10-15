#pragma once
#include <hlclient/game_api/temporary_effects.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <span>
namespace hlclient::app {
struct TemporarySpriteFrame final {
    game_api::TemporaryEffectIdentity identity;
    game_api::TemporarySpriteIntent sprite;
    assets::AssetVector3 origin{};
    double age_seconds{};
    std::uint8_t diagnostic_tag{};
    assets::AssetVector3 velocity{};
};
struct TemporaryStudioFrame final {
    game_api::TemporaryEffectIdentity identity;
    game_api::TemporaryStudioIntent model;
    assets::AssetVector3 origin{};
    double age_seconds{};
};
// Existing particle/light execution and imported sprite materializer; no game
// names, collision, damage, resource IO, or local explosion/smoke timer.
class TemporaryEffectPresentation final {
public:
    void reset(std::uint64_t generation=0) noexcept;
    void consume(const game_api::TemporaryEffectsBatch&,double now) noexcept;
    void present(renderer::RenderScene&,double now) noexcept;
    [[nodiscard]] std::span<const TemporarySpriteFrame> sprites() const noexcept {return sprites_;}
    [[nodiscard]] std::span<const TemporaryStudioFrame> models() const noexcept {return models_;}
    struct Statistics {
        std::uint64_t accepted{}, duplicate{}, stale{}, invalid{}, sprite_evictions{},
            light_evictions{}, light_submissions{}, particle_submissions{}, model_evictions{},
            drops_created{},drop_evictions{};
    };
    [[nodiscard]] const Statistics& statistics() const noexcept {return stats_;}
    [[nodiscard]] Statistics statistics(std::uint8_t tag) const noexcept {
        return tag<tag_stats_.size() ? tag_stats_[tag] : Statistics{};
    }
private:
    std::uint64_t generation_{};
    double now_{};
    std::array<std::optional<game_api::TemporaryEffectIdentity>,256> seen_{};
    std::size_t next_seen_{},next_sprite_{},next_light_{};
    std::array<std::optional<game_api::TemporaryEffectIntent>,32> active_sprites_{};
    std::array<std::optional<game_api::TemporaryEffectIntent>,16> active_lights_{};
    std::array<std::optional<game_api::TemporaryEffectIntent>,32> active_models_{};
    std::array<TemporaryStudioFrame,32> model_buffer_{};
    std::span<const TemporaryStudioFrame> models_{};
    std::size_t next_model_{};
    void update_models() noexcept;
    struct SpriteDrop {
        TemporarySpriteFrame frame;
        double starts_at_seconds{};
    };
    std::array<std::optional<SpriteDrop>,128> active_drops_{};
    std::size_t next_drop_{};
    std::array<TemporarySpriteFrame,160> sprite_buffer_{};
    std::span<const TemporarySpriteFrame> sprites_{};
    renderer::ImpactParticlePool particles_;
    Statistics stats_;
    std::array<Statistics,4> tag_stats_{};
};
}

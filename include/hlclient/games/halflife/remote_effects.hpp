#pragma once
#include <hlclient/game_api/remote_effects.hpp>
#include <hlclient/games/halflife/materials.hpp>

namespace hlclient::games::halflife {
// Session-owned fixed outbox. Local focus/viewmodel/life changes cannot cancel
// another player's already committed event. No prediction or damage authority.
class RemoteWeaponEffects final {
public:
    void reset(std::uint64_t generation,std::optional<std::uint32_t> receiving) noexcept;
    void configure(std::span<const game_api::ScriptedEventBinding>) noexcept;
    void commit(const game_api::CommittedScriptedEvent&) noexcept;
    [[nodiscard]] game_api::RemoteWeaponEffectsBatch drain(double now) noexcept;
    [[nodiscard]] game_api::LocalAudioBatch audio() noexcept;
    void impact(const game_api::LocalWeaponActionIdentity&,assets::AssetVector3,double,
        std::optional<game_api::LocalWorldSurfaceHit>,const HalfLifeMaterials&) noexcept;
    void contact(const game_api::LocalShellContact&) noexcept;
private:
    struct Seen { std::uint64_t record{}; std::uint32_t cursor{},ordinal{}; };
    struct Action { game_api::LocalWeaponActionIdentity identity; bool impact{};
        std::uint8_t contacts{}; };
    void cue(game_api::LocalSoundReference,game_api::LocalSoundKind,assets::AssetVector3,
        double,float,const game_api::LocalWeaponActionIdentity&,std::uint32_t=0U) noexcept;
    std::uint64_t generation_{},serial_{};
    std::uint32_t action_sequence_{};
    std::optional<std::uint32_t> receiving_;
    std::array<game_api::ScriptedEventBinding,32> bindings_{};
    std::size_t binding_count_{};
    std::array<game_api::CommittedScriptedEvent,32> pending_{};
    std::size_t pending_count_{};
    std::array<Seen,128> seen_{};
    std::size_t next_seen_{};
    std::array<std::optional<Action>,64> actions_{};
    std::size_t next_action_{};
    game_api::LocalAudioBatch audio_{};
    game_api::RemoteWeaponEffectStatistics stats_{};
};
} // namespace hlclient::games::halflife

#pragma once
#include <hlclient/game_api/remote_effects.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/games/halflife/materials.hpp>

namespace hlclient::games::halflife {
// Session-owned fixed outbox. Local focus/viewmodel/life changes cannot cancel
// another player's already committed event. No prediction or damage authority.
class RemoteWeaponEffects final {
public:
    void reset(std::uint64_t generation,std::optional<std::uint32_t> receiving,std::uint64_t map_generation=0) noexcept;
    void configure(std::span<const game_api::ScriptedEventBinding>) noexcept;
    void commit(const game_api::CommittedScriptedEvent&) noexcept;
    [[nodiscard]] game_api::RemoteWeaponSocketPlan socket_plan(const game_api::RemoteWeaponModelContext&) noexcept;
    void poses(std::span<const game_api::RemoteWeaponPoseSample>) noexcept;
    void entities(std::span<const client::RuntimePacketEntityObservation>,
        std::uint32_t maximum_clients,std::uint64_t ordinal) noexcept;
    void slot_boundary(std::uint32_t) noexcept;
    [[nodiscard]] game_api::RemoteWeaponEffectsBatch drain(double now) noexcept;
    [[nodiscard]] game_api::LocalAudioBatch audio() noexcept;
    [[nodiscard]] std::optional<assets::AssetVector3> impact(const game_api::LocalWeaponActionIdentity&,assets::AssetVector3,double,
        std::optional<game_api::LocalWorldSurfaceHit>,const HalfLifeMaterials&) noexcept;
    void contact(const game_api::LocalShellContact&) noexcept;
private:
    struct Seen { std::uint64_t record{}; std::uint32_t cursor{},ordinal{}; };
    // One bounded causal shot owner; distinct pellet/contact sub-identities do
    // not grow the history with eight copies of the parent action.
    struct Action { game_api::LocalWeaponActionIdentity identity;
        std::uint8_t pellet_count{1U}, shell_count{1U}; bool buckshot{};
        std::uint16_t impacts{}; std::array<std::uint8_t,2> contacts{};
        std::optional<assets::AssetVector3> crossbow_direction; };
    void cue(game_api::LocalSoundReference,game_api::LocalSoundKind,assets::AssetVector3,
        double,float,const game_api::LocalWeaponActionIdentity&,std::uint32_t=0U,float=1.0F) noexcept;
    std::uint64_t generation_{},serial_{};
    std::uint64_t map_generation_{};
    std::uint32_t action_sequence_{};
    std::optional<std::uint32_t> receiving_;
    std::array<game_api::ScriptedEventBinding,32> bindings_{};
    std::size_t binding_count_{};
    struct Pending { game_api::CommittedScriptedEvent event; std::uint64_t lifetime{}, visual_epoch{}; };
    struct VisualLife { bool observed{}, present{}, inactive{}; std::uint64_t epoch{}, floor{};
        std::optional<std::uint32_t> model; std::uint64_t tracer_count{}; };
    std::array<VisualLife,32> visual_lives_{};
    std::uint64_t entity_ordinal_{};
    std::array<Pending,32> pending_{};
    std::array<game_api::RemoteWeaponPoseSample,32> poses_{};
    std::size_t pose_count_{};
    struct SocketProfile {
        bool used{};
        std::uint64_t resource_id{};
        assets::AssetSourceFingerprint revision{};
        game_api::RemoteWeaponSocketPlan plan;
    };
    std::array<SocketProfile,32> socket_profiles_{};
    std::size_t next_socket_profile_{};
    std::size_t pending_count_{};
    std::array<Seen,128> seen_{};
    std::size_t next_seen_{};
    std::array<std::optional<Action>,64> actions_{};
    std::size_t next_action_{};
    game_api::LocalAudioBatch audio_{};
    game_api::RemoteWeaponEffectStatistics stats_{};
};
} // namespace hlclient::games::halflife

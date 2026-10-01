#pragma once
#include <hlclient/game_api/audio.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/game_api/command.hpp>
#include <hlclient/game_api/action_evidence.hpp>
#include <hlclient/game_api/movement_policy.hpp>
#include <hlclient/game_api/movement_audio.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/remote_player.hpp>
#include <hlclient/game_api/remote_effects.hpp>
#include <hlclient/game_api/local_visuals.hpp>
#include <hlclient/game_api/scenario.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::game_api {
struct GameSessionIdentity final {
    std::uint64_t network_generation{}, map_generation{};
    std::optional<std::uint32_t> receiving_entity;
    friend bool operator==(const GameSessionIdentity&, const GameSessionIdentity&) = default;
};
enum class GameMessageKind : std::uint8_t { user_message, weapon_animation };
// Borrowed only for stage_record(). Name/body refer to the exact bounded owning
// decoded record. Implementations must copy anything retained beyond that call.
struct GameMessageView final {
    GameMessageKind kind{};
    std::string_view name;
    std::span<const std::byte> body;
    client::RuntimeObservationSource source;
    std::optional<std::uint8_t> registration_id;
};
enum class GameMessageErrorCode : std::uint8_t {
    invalid_body, invalid_identifier, invalid_token, invalid_value, capacity_exceeded
};
[[nodiscard]] inline std::string_view to_string(GameMessageErrorCode code) noexcept {
    switch (code) {
    case GameMessageErrorCode::invalid_body: return "invalid_body";
    case GameMessageErrorCode::invalid_identifier: return "invalid_identifier";
    case GameMessageErrorCode::invalid_token: return "invalid_token";
    case GameMessageErrorCode::invalid_value: return "invalid_value";
    case GameMessageErrorCode::capacity_exceeded: return "capacity_exceeded";
    }
    return "unavailable";
}
// Owning bounded diagnostic only; never retains a body or borrowed RX name.
struct GameMessageFailure final {
    GameMessageErrorCode code{};
    std::string message_name;
    std::optional<std::uint8_t> registration_id;
    client::RuntimeObservationSource source;
    std::optional<std::size_t> expected_body_size;
    std::size_t actual_body_size{};
    std::string context; // Project-authored internal explanation, not exported.
    std::optional<std::size_t> body_byte_offset;
};
struct GameRecordInput final {
    // Null denotes initial/new-generation staging, even while the module still
    // owns the prior committed generation. reset/commit happen only after the
    // complete record and owning candidate have passed host validation.
    const client::RuntimeClientObservationState* previous{};
    const client::RuntimeClientObservationState& observation;
    std::span<const GameMessageView> messages;
    std::optional<std::uint32_t> receiving_entity;
};
// Existing owning projections are reused; no second copy of delta or network
// state crosses the seam. The host validates these before atomic publication.
struct GameRecordState final {
    client::RuntimeWeaponHudObservation weapon_hud;
    std::vector<client::RuntimeLifeEvent> life_events;
    client::LocalPlayerLifecycle lifecycle;
    // Owning, one-record presentation events, never counter/ownership writes.
    // The host bounds/validates their source before the existing atomic commit.
    struct Pickup final {
        enum class Kind : std::uint8_t { ammunition, weapon, item };
        Kind kind{};
        std::uint8_t identifier{};
        std::optional<std::uint8_t> amount;
        std::string item_token;
        client::RuntimeObservationSource source;
    };
    std::vector<Pickup> pickups;
    std::optional<client::RuntimeObservationSource> pickup_high_water;
};
struct GameRecordResult final {
    std::optional<GameRecordState> state;
    std::optional<std::string> error;
    std::optional<GameMessageFailure> message_failure;
    explicit operator bool() const noexcept { return state.has_value() && !error; }
};
class IGameClientModule {
public:
    virtual ~IGameClientModule() = default;
    virtual void reset(GameSessionIdentity) noexcept = 0;
    virtual void teardown() noexcept = 0;
    [[nodiscard]] virtual const GameMovementPolicy& movement_policy() const noexcept = 0;
    [[nodiscard]] virtual GameRecordResult stage_record(const GameRecordInput&) const = 0;
    virtual void commit_record(GameRecordState&&) noexcept = 0;
    virtual void bind_model(std::optional<LocalWeaponModelMetadata>) = 0;
    virtual void observe(const client::RuntimeClientObservationState&, double) = 0;
    virtual void submit(const LocalWeaponSubmittedCommand&, double) = 0;
    virtual void cancel_uncommitted() noexcept = 0;
    [[nodiscard]] virtual LocalWeaponPresentationSnapshot sample(double) noexcept = 0;
    [[nodiscard]] virtual LocalAudioBatch drain_audio() noexcept = 0;
    virtual void configure_scripted_events(std::span<const ScriptedEventBinding>) noexcept {}
    virtual void committed_scripted_event(const CommittedScriptedEvent&) noexcept {}
    [[nodiscard]] virtual RemoteWeaponEffectsBatch drain_remote_effects(double) noexcept { return {}; }
    [[nodiscard]] virtual LocalAudioBatch drain_remote_audio() noexcept { return {}; }
    virtual void remote_world_impact(const LocalWeaponActionIdentity&, assets::AssetVector3,
        double, std::optional<LocalWorldSurfaceHit>) noexcept {}
    virtual void remote_shell_contact(const LocalShellContact&) noexcept {}
    [[nodiscard]] virtual RemotePlayerPresentationPolicy remote_player_policy() const noexcept {
        return {};
    }
    [[nodiscard]] virtual RemotePlayerPresentationIntent remote_player(
        const RemotePlayerPresentationContext&) noexcept { return {}; }
    [[nodiscard]] virtual LocalVisualFrame local_visuals(const LocalVisualContext&) noexcept {
        return {};
    }
    [[nodiscard]] virtual std::optional<LocalImpactAssetProfile> local_impact_assets() const noexcept {
        return {};
    }
    [[nodiscard]] virtual std::optional<LocalCrowbarImpactAssetProfile>
    local_crowbar_impact_assets() const noexcept { return {}; }
    virtual void accepted_world_impact(const LocalWeaponActionIdentity&,
        assets::AssetVector3, double, std::optional<LocalWorldSurfaceHit>) noexcept {}
    [[nodiscard]] virtual bool resolved_world_impact(const LocalWeaponActionIdentity&,
        LocalWorldImpactOutcome, std::optional<assets::AssetVector3>, double,
        std::optional<LocalWorldSurfaceHit>) noexcept { return false; }
    [[nodiscard]] virtual std::optional<LocalWorldImpactDiagnostic>
    last_world_impact_diagnostic() const noexcept { return {}; }
    virtual void shell_contact(const LocalShellContact&) noexcept {}
    // Optional presentation seam. Replay calls update deterministic game
    // state but must not publish audio. Core/alternate modules can stay silent.
    virtual void configure_movement_materials(std::string_view) noexcept {}
    // Immutable bounded virtual tokens, borrowed only during this call/startup.
    // The host executes preparation through its existing approved audio loader.
    [[nodiscard]] virtual std::span<const LocalSoundReference>
    movement_sound_preparation() const noexcept { return {}; }
    virtual void rewind_movement_audio(std::uint32_t) noexcept {}
    virtual void observe_movement_audio(const MovementAudioObservation&, bool) noexcept {}
    [[nodiscard]] virtual ViewmodelIntent viewmodel(
        const client::RuntimeClientObservationState&,
        std::optional<LocalWeaponModelMetadata>, double,
        std::optional<LocalWeaponVisual>) = 0;
    [[nodiscard]] virtual HudState hud(const client::RuntimeClientObservationState&, double) = 0;
    [[nodiscard]] virtual CameraIntent camera(const client::RuntimeClientObservationState&, double) const noexcept = 0;
    virtual void observe_action_evidence(const client::RuntimeClientObservationState&,
        const std::optional<GameActionTraffic>&) noexcept = 0;
    [[nodiscard]] virtual GameActionEvidenceSnapshot action_evidence() const noexcept = 0;
    [[nodiscard]] virtual std::optional<GameCommandRequest> inventory_selection(
        const client::RuntimeClientObservationState&, std::uint8_t) const = 0;
    [[nodiscard]] virtual std::optional<std::uint8_t> select_group(
        const client::RuntimeClientObservationState&, std::uint8_t,
        std::optional<std::uint8_t>) const = 0;
    [[nodiscard]] virtual std::optional<std::uint8_t> cycle_inventory(
        const client::RuntimeClientObservationState&, std::optional<std::uint8_t>, int) const = 0;
    [[nodiscard]] virtual GameCommandRequest self_kill_request() const = 0;
    [[nodiscard]] virtual std::optional<std::uint8_t> scenario_inventory_target(
        const client::RuntimeClientObservationState&, std::size_t) const = 0;
    [[nodiscard]] virtual GameScenarioDirective scenario_command(
        GameScenario, const client::RuntimeClientObservationState*, std::size_t, double, double) noexcept = 0;
    [[nodiscard]] virtual DamageRespawnScriptSnapshot damage_respawn_snapshot() const noexcept = 0;
};
} // namespace hlclient::game_api

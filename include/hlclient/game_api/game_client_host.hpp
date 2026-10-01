#pragma once
#include <hlclient/game_api/game_client_module.hpp>

namespace hlclient::game_api {
// A single caller-owned module per game session. This host has no engine loop,
// scene, transport, renderer or concrete game dependency. Composition retains it
// across record/frame consumers; destruction tears down exactly once.
class GameClientHost final {
public:
    explicit GameClientHost(std::unique_ptr<IGameClientModule>);
    ~GameClientHost();
    GameClientHost(const GameClientHost&) = delete;
    GameClientHost& operator=(const GameClientHost&) = delete;
    void reset(GameSessionIdentity) noexcept;
    void teardown() noexcept;
    [[nodiscard]] const GameMovementPolicy& movement_policy() const noexcept;
    [[nodiscard]] GameRecordResult stage_record(const GameRecordInput&) const;
    void commit_record(GameRecordState&&) noexcept;
    void bind_model(std::optional<LocalWeaponModelMetadata>);
    void observe(const client::RuntimeClientObservationState&, double);
    void submit(const LocalWeaponSubmittedCommand&, double);
    void cancel_uncommitted() noexcept;
    [[nodiscard]] LocalWeaponPresentationSnapshot sample(double) noexcept;
    [[nodiscard]] LocalAudioBatch drain_audio() noexcept;
    void configure_scripted_events(std::span<const ScriptedEventBinding>) noexcept;
    void committed_scripted_event(const CommittedScriptedEvent&) noexcept;
    [[nodiscard]] RemoteWeaponEffectsBatch drain_remote_effects(double) noexcept;
    [[nodiscard]] LocalAudioBatch drain_remote_audio() noexcept;
    void remote_world_impact(const LocalWeaponActionIdentity&, assets::AssetVector3,
        double, std::optional<LocalWorldSurfaceHit> = {}) noexcept;
    void remote_shell_contact(const LocalShellContact&) noexcept;
    [[nodiscard]] RemotePlayerPresentationPolicy remote_player_policy() const noexcept;
    [[nodiscard]] RemotePlayerPresentationIntent remote_player(
        const RemotePlayerPresentationContext&) noexcept;
    [[nodiscard]] LocalVisualFrame local_visuals(const LocalVisualContext&) noexcept;
    [[nodiscard]] std::optional<LocalImpactAssetProfile> local_impact_assets() const noexcept;
    [[nodiscard]] std::optional<LocalCrowbarImpactAssetProfile> local_crowbar_impact_assets() const noexcept;
    void accepted_world_impact(const LocalWeaponActionIdentity&, assets::AssetVector3, double,
        std::optional<LocalWorldSurfaceHit> = {}) noexcept;
    [[nodiscard]] bool resolved_world_impact(const LocalWeaponActionIdentity&, LocalWorldImpactOutcome,
        std::optional<assets::AssetVector3>, double,
        std::optional<LocalWorldSurfaceHit> = {}) noexcept;
    [[nodiscard]] std::optional<LocalWorldImpactDiagnostic>
    last_world_impact_diagnostic() const noexcept;
    void shell_contact(const LocalShellContact&) noexcept;
    void configure_movement_materials(std::string_view) noexcept;
    [[nodiscard]] std::span<const LocalSoundReference> movement_sound_preparation() const noexcept;
    void rewind_movement_audio(std::uint32_t) noexcept;
    void observe_movement_audio(const MovementAudioObservation&, bool replay) noexcept;
    void set_movement_audio_time_origin(double steady_clock_seconds) noexcept;
    [[nodiscard]] ViewmodelIntent viewmodel(const client::RuntimeClientObservationState&,
        std::optional<LocalWeaponModelMetadata>, double, std::optional<LocalWeaponVisual> = {});
    [[nodiscard]] HudState hud(const client::RuntimeClientObservationState&, double);
    [[nodiscard]] CameraIntent camera(const client::RuntimeClientObservationState&, double) const noexcept;
    void observe_action_evidence(const client::RuntimeClientObservationState&,
        const std::optional<GameActionTraffic>&) noexcept;
    [[nodiscard]] GameActionEvidenceSnapshot action_evidence() const noexcept;
    [[nodiscard]] std::optional<GameCommandRequest> inventory_selection(
        const client::RuntimeClientObservationState&, std::uint8_t) const;
    [[nodiscard]] std::optional<std::uint8_t> select_group(
        const client::RuntimeClientObservationState&, std::uint8_t,
        std::optional<std::uint8_t>) const;
    [[nodiscard]] std::optional<std::uint8_t> cycle_inventory(
        const client::RuntimeClientObservationState&, std::optional<std::uint8_t>, int) const;
    [[nodiscard]] GameCommandRequest self_kill_request() const;
    [[nodiscard]] std::optional<std::uint8_t> scenario_inventory_target(
        const client::RuntimeClientObservationState&, std::size_t) const;
    [[nodiscard]] GameScenarioDirective scenario_command(
        GameScenario, const client::RuntimeClientObservationState*, std::size_t, double, double = 0.0) noexcept;
    [[nodiscard]] DamageRespawnScriptSnapshot damage_respawn_snapshot() const noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
private:
    std::uint64_t audio_session_{1};
    std::optional<double> movement_audio_time_origin_;
    std::uint64_t movement_audio_observations_{}, movement_audio_clock_dropped_{},
        movement_audio_invalid_time_{};
    std::unique_ptr<IGameClientModule> module_;
    bool active_{true};
};
}

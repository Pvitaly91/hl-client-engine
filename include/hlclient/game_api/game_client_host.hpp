#pragma once
#include <hlclient/game_api/game_client_module.hpp>
#include <functional>

namespace hlclient::game_api {
// A single caller-owned module per game session. This host has no engine loop,
// scene, transport, renderer or concrete game dependency. Composition retains it
// across record/frame consumers; destruction tears down exactly once.
class GameClientHost final : private WeaponDiagnosticObserver {
public:
    // A source can commit several initialization subrecords inside one owning
    // payload. Only their console publication is held until that payload passes.
    class ConsolePublicationBatch final {
    public:
        explicit ConsolePublicationBatch(GameClientHost*) noexcept;
        ~ConsolePublicationBatch();
        ConsolePublicationBatch(const ConsolePublicationBatch&)=delete;
        ConsolePublicationBatch& operator=(const ConsolePublicationBatch&)=delete;
        void accept() noexcept {accepted_=true;}
    private:
        GameClientHost* host_{};std::uint64_t serial_{};bool accepted_{};
    };
    explicit GameClientHost(std::unique_ptr<IGameClientModule>);
    ~GameClientHost();
    GameClientHost(const GameClientHost&) = delete;
    GameClientHost& operator=(const GameClientHost&) = delete;
    void reset(GameSessionIdentity) noexcept;
    // Borrowed session-thread sink; caller must detach it before its destruction.
    // Disabled means no per-command clock reads or diagnostic state retention.
    void set_diagnostic_sink(core::MultiplayerDiagnosticSink*) noexcept;
    // Diagnostic identity only: the game module keeps its existing map epoch.
    // Captures are caller-owned; detaching the sink clears this callback.
    void set_diagnostic_generations(std::function<std::array<std::uint64_t,2>()>) noexcept;
    [[nodiscard]] std::span<const WeaponDiagnosticCapability> weapon_diagnostic_capabilities() const noexcept;
    void teardown() noexcept;
    [[nodiscard]] const GameMovementPolicy& movement_policy() const noexcept;
    [[nodiscard]] ConsoleVisualTheme console_theme() const noexcept;
    [[nodiscard]] std::string_view text_message_preparation() const noexcept;
    [[nodiscard]] bool bind_text_messages(const TextMessageCatalogue&);
    [[nodiscard]] gameplay_input::GameplayButtonMask edge_triggered_buttons(
        const client::RuntimeClientObservationState&) const noexcept;
    [[nodiscard]] GameRecordResult stage_record(const GameRecordInput&) const;
    void commit_record(GameRecordState&&) noexcept;
    void bind_model(std::optional<LocalWeaponModelMetadata>);
    [[nodiscard]] std::span<const std::string_view> local_model_preparation() const noexcept;
    [[nodiscard]] bool bind_model_variants(std::span<const LocalWeaponModelMetadata>);
    void observe(const client::RuntimeClientObservationState&, double);
    void submit(const LocalWeaponSubmittedCommand&, double);
    void prepare_presentation(const IStaticEffectTrace&, double) noexcept;
    std::optional<assets::AssetVector3> command_movement_effect(const LocalWeaponSubmittedCommand&) noexcept;
    [[nodiscard]] CommandMovementEffectPhase command_movement_effect_phase() const noexcept;
    BeamEffectsFrame beam_effects(const IStaticEffectTrace&,double,std::optional<assets::AssetVector3> = {}) noexcept;
    [[nodiscard]] BeamRibbonGlowProfile beam_ribbon_glow_profile(std::string_view) const noexcept;
    [[nodiscard]] BeamSceneLightProfile beam_scene_light_profile(std::string_view, bool sine_strand) const noexcept;
    [[nodiscard]] std::optional<EntityTrailProfile> entity_trail_profile(
        std::string_view model, std::string_view sprite) const noexcept;
    void cancel_uncommitted() noexcept;
    [[nodiscard]] LocalWeaponPresentationSnapshot sample(double) noexcept;
    [[nodiscard]] ScreenEffectsFrame screen_effects(double) const noexcept;
    [[nodiscard]] LocalAudioBatch drain_audio() noexcept;
    void configure_scripted_events(std::span<const ScriptedEventBinding>) noexcept;
    void committed_scripted_event(const CommittedScriptedEvent&) noexcept;
    void committed_temporary_effect(const client::CommittedTemporaryEffect&) noexcept;
    void bind_effect_resource(const EffectResourceBinding&) noexcept;
    [[nodiscard]] std::span<const std::string_view> effect_sprite_preparation() const noexcept;
    [[nodiscard]] std::string_view effect_palette_preparation() const noexcept;
    [[nodiscard]] std::span<const DecalAssetRequest> surface_decal_preparation() const noexcept;
    void bind_effect_palette(const EffectPaletteMetadata&) noexcept;
    void configure_effect_policy(const EffectPresentationPolicy&) noexcept;
    [[nodiscard]] std::string_view temporary_effect_source_label(TemporaryEffectIdentity::Source) const noexcept;
    void committed_sound(const CommittedSoundObservation&) noexcept;
    struct TemporaryDeliveryStatistics {std::uint64_t received{},forwarded{},clock_dropped{};};
    [[nodiscard]] TemporaryDeliveryStatistics temporary_delivery_statistics(client::TemporaryEffectKind kind) const noexcept {
        const auto index=static_cast<std::size_t>(kind);
        return index<temporary_delivery_.size() ? temporary_delivery_[index] : TemporaryDeliveryStatistics{};
    }
    // Generic server follow effects bypass game semantics. Owning fixed outbox,
    // relative host clock; exactly one drain in the normal application path.
    struct FollowEvents { std::array<client::CommittedTemporaryEffect,32> events{}; std::size_t count{}; std::uint64_t dropped{}; };
    [[nodiscard]] FollowEvents drain_follow_events() noexcept;
    [[nodiscard]] TemporaryEffectsBatch drain_temporary_effects(double) noexcept;
    [[nodiscard]] std::span<const LocalSoundReference> temporary_sound_preparation() const noexcept;
    [[nodiscard]] RemoteWeaponSocketPlan remote_weapon_socket_plan(const RemoteWeaponModelContext&) noexcept;
    void remote_weapon_poses(std::span<const RemoteWeaponPoseSample>) noexcept;
    void remote_entities(std::span<const client::RuntimePacketEntityObservation>,
        std::uint32_t, std::uint64_t) noexcept;
    void remote_slot_boundary(std::uint32_t) noexcept;
    [[nodiscard]] WorldEntityPresentationIntent world_entity(
        const WorldEntityPresentationContext&) const noexcept;
    [[nodiscard]] RemoteWeaponEffectsBatch drain_remote_effects(double) noexcept;
    [[nodiscard]] LocalAudioBatch drain_remote_audio() noexcept;
    void remote_world_impact(const LocalWeaponActionIdentity&, assets::AssetVector3,
        double, std::optional<LocalWorldSurfaceHit> = {}) noexcept;
    void remote_shell_contact(const LocalShellContact&) noexcept;
    [[nodiscard]] RemotePlayerPresentationPolicy remote_player_policy() const noexcept;
    [[nodiscard]] PlayerAppearanceSelection player_appearance(std::uint32_t, std::string_view) const;
    [[nodiscard]] std::vector<PlayerPaletteRange> player_palette(
        const PlayerAppearanceFields&, const assets::SkeletalModelAssetData&) const;
    [[nodiscard]] bool player_model_compatible(const assets::SkeletalModelAssetData&) const noexcept;
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
    void set_hud_context(HudPresentationContext) noexcept;
    [[nodiscard]] std::span<const HudManifestRequest> hud_resource_preparation() const noexcept;
    [[nodiscard]] bool bind_hud_resources(const HudResourceSet&);
    [[nodiscard]] HudMenuResult hud_menu_input(
        const client::RuntimeClientObservationState&, const HudMenuInput&) noexcept;
    void hud_selection_result(std::uint8_t,bool,double) noexcept;
    [[nodiscard]] std::span<const LocalSoundReference> hud_sound_preparation() const noexcept;
    [[nodiscard]] std::optional<double> presentation_seconds(double monotonic_seconds) const noexcept;
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
    [[nodiscard]] std::uint64_t reset_serial() const noexcept { return audio_session_; }
private:
    [[nodiscard]] bool weapon_diagnostics_enabled() const noexcept override;
    void weapon_action_observed(const WeaponActionDiagnosticObservation&) noexcept override;
    [[nodiscard]] core::MultiplayerDiagnosticEvent diagnostic_event() noexcept;
    void diagnostic_observe(const client::RuntimeClientObservationState&,double) noexcept;
    void diagnostic_submit(const LocalWeaponSubmittedCommand&) noexcept;
    void diagnostic_audio(const LocalAudioBatch&) noexcept;
    void diagnostic_entities(std::span<const client::RuntimePacketEntityObservation>,std::uint32_t,std::uint64_t) noexcept;
    [[nodiscard]] bool diagnostic_source_once(std::uint8_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint32_t) noexcept;
    struct DiagnosticSource {
        std::uint8_t kind{};std::uint64_t record{},ordinal{},bit{};std::uint32_t entry{};
        friend bool operator==(const DiagnosticSource&,const DiagnosticSource&)=default;
    };
    struct DiagnosticObjectResource {
        std::uint16_t model{};WeaponDiagnosticObjectProfile profile{};
    };
    struct DiagnosticObject {
        std::uint32_t entity{},model{};
        WeaponDiagnosticObjectProfile profile{};
        std::uint64_t lifetime{},seen_ordinal{};
        std::optional<std::uint32_t> owner;
        std::optional<std::array<float,3>> origin;
    };
    struct DiagnosticActionReceipt {
        LocalWeaponActionIdentity identity;std::uint64_t id{};
        bool accepted{};
    };
    struct DiagnosticState {
        std::optional<std::uint8_t> weapon;
        std::optional<std::int32_t> clip,reserve,secondary_reserve;
        std::optional<bool> reloading;
        std::optional<double> primary_timer,secondary_timer,client_timer,fov,health;
        std::optional<client::RuntimeObservationSource> source;
        std::uint64_t revision{},life{};
        bool dead{};
    };
    core::MultiplayerDiagnosticSink* diagnostic_sink_{};
    std::function<std::array<std::uint64_t,2>()> diagnostic_generations_;
    std::optional<std::array<std::uint64_t,2>> diagnostic_bound_generations_;
    GameSessionIdentity diagnostic_identity_;
    DiagnosticState diagnostic_state_;
    std::array<std::optional<DiagnosticActionReceipt>,128> diagnostic_actions_{};
    std::array<std::optional<DiagnosticSource>,128> diagnostic_sources_{};
    std::size_t diagnostic_source_next_{};
    std::array<std::optional<DiagnosticObjectResource>,32> diagnostic_object_resources_{};
    std::array<std::optional<DiagnosticObject>,128> diagnostic_objects_{};
    std::array<std::uint32_t,4096> diagnostic_entity_models_{};
    std::uint64_t diagnostic_object_lifetime_{},diagnostic_entity_ordinal_{};
    std::size_t diagnostic_action_next_{};
    std::uint64_t diagnostic_serial_{},diagnostic_action_serial_{},diagnostic_last_intent_ns_{};
    std::uint32_t diagnostic_command_floor_{};
    std::uint16_t diagnostic_previous_buttons_{};
    std::array<char,48> diagnostic_reject_reason_{};
    std::uint64_t diagnostic_reject_ns_{};
    std::uint32_t diagnostic_reject_weapon_{},diagnostic_reject_mode_{},diagnostic_reject_suppressed_{};
    std::uint64_t audio_session_{1};
    std::optional<client::RuntimeObservationSource> console_text_high_water_;
    void finish_console_publication(bool) noexcept;
    std::vector<GameRecordState::ConsoleText> deferred_console_text_;
    std::size_t deferred_console_bytes_{},deferred_console_dropped_{};
    bool console_publication_deferred_{};
    std::optional<double> movement_audio_time_origin_;
    std::uint64_t movement_audio_observations_{}, movement_audio_clock_dropped_{},
        movement_audio_invalid_time_{};
    std::unique_ptr<IGameClientModule> module_;
    bool active_{true};
    FollowEvents follow_events_;
    std::uint64_t follow_generation_{};
    std::array<TemporaryDeliveryStatistics,8> temporary_delivery_{};
};
}

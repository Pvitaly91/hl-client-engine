#pragma once
#include <hlclient/game_api/hud_input.hpp>
#include <hlclient/game_api/hud_resources.hpp>
#include <hlclient/game_api/audio.hpp>
#include <hlclient/game_api/effect_palette.hpp>
#include <hlclient/game_api/beam_effects.hpp>
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
#include <hlclient/game_api/multiplayer.hpp>
#include <hlclient/game_api/temporary_effects.hpp>
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
enum class GameMessageKind : std::uint8_t { user_message, weapon_animation, player_display };
// Borrowed only for stage_record(). Name/body refer to the exact bounded owning
// decoded record. Implementations must copy anything retained beyond that call.
struct GameMessageView final {
    GameMessageKind kind{};
    std::string_view name;
    std::span<const std::byte> body;
    client::RuntimeObservationSource source;
    std::optional<std::uint8_t> registration_id;
    std::optional<PlayerDisplayUpdate> player_display;
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
    std::uint32_t maximum_clients{32U};
    std::span<const PlayerDisplayUpdate> initial_players{};
    std::optional<double> presentation_seconds;
};
// Existing owning projections are reused; no second copy of delta or network
// state crosses the seam. The host validates these before atomic publication.
struct GameRecordState final {
    struct ScreenShake { double amplitude{},duration_seconds{},frequency_hz{}; client::RuntimeObservationSource source; };
    // Current committed record only; visual application is independently optional.
    std::vector<ScreenShake> screen_shakes;
    struct ScreenFade {
        double duration_seconds{}, hold_seconds{};
        std::array<float,4> rgba{};
        bool fade_out{}, modulate{}, stay_out{};
        client::RuntimeObservationSource source;
    };
    std::vector<ScreenFade> screen_fades;
    std::optional<double> screen_effect_seconds;
    MultiplayerHudState multiplayer;
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
    // Only pending press edges contribute these buttons BEFORE history insert.
    // No mutation of submitted/history/replay commands. Default preserves hold.
    [[nodiscard]] virtual gameplay_input::GameplayButtonMask edge_triggered_buttons(
        const client::RuntimeClientObservationState&) const noexcept { return 0U; }
    [[nodiscard]] virtual GameRecordResult stage_record(const GameRecordInput&) const = 0;
    virtual void commit_record(GameRecordState&&) noexcept = 0;
    virtual void bind_model(std::optional<LocalWeaponModelMetadata>) = 0;
    // Compiled module tokens, borrowed for its lifetime. Host resolves only
    // exact already-approved session resources; no path/root handle crosses API.
    [[nodiscard]] virtual std::span<const std::string_view> local_model_preparation() const noexcept { return {}; }
    // At most eight owning metadata variants. Module copies retained data.
    virtual void bind_model_variants(std::span<const LocalWeaponModelMetadata>) {}
    virtual void observe(const client::RuntimeClientObservationState&, double) = 0;
    virtual void submit(const LocalWeaponSubmittedCommand&, double) = 0;
    // Once before sampling a frame: consume bounded owning presentation requests.
    // Query is borrowed only for this call, never retained by a module.
    virtual void prepare_presentation(const IStaticEffectTrace&, double) noexcept {}
    // Once after validated wire history insert, before simulation. An impulse
    // belongs to that immutable prediction command; no wire/event replay write.
    virtual std::optional<assets::AssetVector3> command_movement_effect(const LocalWeaponSubmittedCommand&) noexcept { return {}; }
    [[nodiscard]] virtual CommandMovementEffectPhase command_movement_effect_phase() const noexcept {
        return CommandMovementEffectPhase::before_movement;
    }
    virtual BeamEffectsFrame beam_effects(const IStaticEffectTrace&,double,std::optional<assets::AssetVector3>) noexcept { return {}; }
    [[nodiscard]] virtual BeamRibbonGlowProfile beam_ribbon_glow_profile(std::string_view) const noexcept { return {}; }
    [[nodiscard]] virtual BeamSceneLightProfile beam_scene_light_profile(std::string_view, bool /* sine_strand */) const noexcept { return {}; }
    [[nodiscard]] virtual ScreenEffectsFrame screen_effects(double) const noexcept { return {}; }
    [[nodiscard]] virtual std::optional<EntityTrailProfile> entity_trail_profile(
        std::string_view, std::string_view) const noexcept { return {}; }
    virtual void cancel_uncommitted() noexcept = 0;
    [[nodiscard]] virtual LocalWeaponPresentationSnapshot sample(double) noexcept = 0;
    [[nodiscard]] virtual LocalAudioBatch drain_audio() noexcept = 0;
    virtual void configure_scripted_events(std::span<const ScriptedEventBinding>) noexcept {}
    virtual void committed_scripted_event(const CommittedScriptedEvent&) noexcept {}
    virtual void committed_temporary_effect(const client::CommittedTemporaryEffect&) noexcept {}
    virtual void bind_effect_resource(const EffectResourceBinding&) noexcept {}
    // Compiled virtual dependency token, never a native path or server-open request.
    [[nodiscard]] virtual std::span<const std::string_view> effect_sprite_preparation() const noexcept {return {};}
    [[nodiscard]] virtual std::string_view effect_palette_preparation() const noexcept {return {};}
    virtual void bind_effect_palette(const EffectPaletteMetadata&) noexcept {}
    virtual void configure_effect_policy(const EffectPresentationPolicy&) noexcept {}
    [[nodiscard]] virtual std::string_view temporary_effect_source_label(TemporaryEffectIdentity::Source) const noexcept {return {};}
    virtual void committed_sound(const CommittedSoundObservation&) noexcept {}
    [[nodiscard]] virtual TemporaryEffectsBatch drain_temporary_effects(double) noexcept { return {}; }
    // Borrowed immutable references, valid for the module lifetime. Startup
    // preparation only; the host owns approved loading and audio output.
    [[nodiscard]] virtual std::span<const LocalSoundReference> temporary_sound_preparation() const noexcept { return {}; }
    [[nodiscard]] virtual std::span<const DecalAssetRequest> surface_decal_preparation() const noexcept { return {}; }
    [[nodiscard]] virtual RemoteWeaponSocketPlan remote_weapon_socket_plan(const RemoteWeaponModelContext&) noexcept { return {}; }
    virtual void remote_weapon_poses(std::span<const RemoteWeaponPoseSample>) noexcept {}
    // Post-transaction notification, once per fresh entity publication. Bounds
    // are the existing validated entity snapshot; retained state must be owned.
    virtual void remote_entities(std::span<const client::RuntimePacketEntityObservation>,
        std::uint32_t, std::uint64_t) noexcept {}
    virtual void remote_slot_boundary(std::uint32_t) noexcept {}
    [[nodiscard]] virtual WorldEntityPresentationIntent world_entity(
        const WorldEntityPresentationContext&) const noexcept { return {}; }
    [[nodiscard]] virtual RemoteWeaponEffectsBatch drain_remote_effects(double) noexcept { return {}; }
    [[nodiscard]] virtual LocalAudioBatch drain_remote_audio() noexcept { return {}; }
    virtual void remote_world_impact(const LocalWeaponActionIdentity&, assets::AssetVector3,
        double, std::optional<LocalWorldSurfaceHit>) noexcept {}
    virtual void remote_shell_contact(const LocalShellContact&) noexcept {}
    [[nodiscard]] virtual RemotePlayerPresentationPolicy remote_player_policy() const noexcept {
        return {};
    }
    [[nodiscard]] virtual PlayerAppearanceSelection player_appearance(
        std::uint32_t, std::string_view) const { return {}; }
    [[nodiscard]] virtual std::vector<PlayerPaletteRange> player_palette(
        const PlayerAppearanceFields&, const assets::SkeletalModelAssetData&) const { return {}; }
    [[nodiscard]] virtual bool player_model_compatible(const assets::SkeletalModelAssetData&) const noexcept { return false; }
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
    virtual void set_hud_context(HudPresentationContext) noexcept {}
    [[nodiscard]] virtual std::span<const HudManifestRequest> hud_resource_preparation() const noexcept { return {}; }
    virtual void bind_hud_resources(const HudResourceSet&) {}
    [[nodiscard]] virtual HudMenuResult hud_menu_input(
        const client::RuntimeClientObservationState&, const HudMenuInput&) noexcept { return {}; }
    virtual void hud_selection_result(std::uint8_t,bool,double) noexcept {}
    [[nodiscard]] virtual std::span<const LocalSoundReference> hud_sound_preparation() const noexcept { return {}; }
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

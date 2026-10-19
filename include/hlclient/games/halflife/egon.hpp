#pragma once
#include <hlclient/game_api/game_client_module.hpp>
namespace hlclient::games::halflife {
inline constexpr game_api::BeamRibbonGlowProfile egon_beam_glow{4.F,.35F};
inline constexpr game_api::BeamSceneLightProfile egon_beam_light{192.F,.65F,192.F};
// The server pulses its ribbon's rendercolor/renderamt down to very dim
// values. Give the observed main strand the same scene emission as the local
// beam, while preserving those original ribbon presentation values.
inline constexpr game_api::BeamSceneLightProfile egon_observer_beam_light{
    192.F,.65F*.7F,192.F,std::array<float,3>{.5F,.5F,1.F},false};
[[nodiscard]] bool egon_sequences(const game_api::LocalWeaponModelMetadata&) noexcept;
// One session owner; server ammo/damage never change here. No engine/RX pointer.
class EgonPresentation final {
public:
    void set_weapon_diagnostic_observer(game_api::WeaponDiagnosticObserver* observer) noexcept { diagnostic_observer_=observer; }
    void reset(game_api::GameSessionIdentity) noexcept;
    void configure(std::span<const game_api::ScriptedEventBinding>) noexcept;
    bool handles(std::uint16_t index) const noexcept {return index && (index==fire_event_ || index==stop_event_);}
    void bind(const std::optional<game_api::LocalWeaponModelMetadata>&);
    void observe(const client::RuntimeClientObservationState&,double) noexcept;
    void command(const game_api::LocalWeaponSubmittedCommand&) noexcept;
    void event(const game_api::CommittedScriptedEvent&) noexcept;
    void poses(std::span<const game_api::RemoteWeaponPoseSample>) noexcept;
    void entities(std::span<const client::RuntimePacketEntityObservation>,std::uint64_t) noexcept;
    void slot_boundary(std::uint32_t) noexcept;
    void cancel(double=0) noexcept;
    bool selected() const noexcept {return selected_;}
    game_api::LocalWeaponPresentationSnapshot sample(double) noexcept;
    void aim(const game_api::LocalVisualContext&) noexcept;
    void append_audio(game_api::LocalAudioBatch&) noexcept;
    void append_frame(const game_api::IStaticEffectTrace&,double,std::optional<assets::AssetVector3>,game_api::BeamEffectsFrame&) noexcept;
    game_api::RemoteWeaponSocketPlan socket_plan(const game_api::RemoteWeaponModelContext&) noexcept;
    static std::span<const game_api::LocalSoundReference> resources() noexcept;
private:
    game_api::WeaponDiagnosticObserver* diagnostic_observer_{};
    struct Emitter {std::uint64_t voice{};double started{};bool active{},running{},narrow{};std::optional<assets::AssetVector3> origin;};
    void start(std::uint32_t,double,bool,std::optional<assets::AssetVector3>) noexcept;
    void stop(std::uint32_t,double,bool) noexcept;
    void control(std::uint32_t,game_api::LocalVoiceControl::Operation,double) noexcept;
    void animate(std::uint32_t,double) noexcept;
    game_api::GameSessionIdentity identity_{};
    std::optional<game_api::LocalWeaponModelMetadata> model_;
    game_api::LocalWeaponPresentationSnapshot snapshot_{};
    std::optional<game_api::LocalVisualContext> aim_;
    std::array<Emitter,33> emitters_{};
    std::array<game_api::LocalSoundCue,32> cues_{};
    std::array<game_api::LocalVoiceControl,32> controls_{};
    std::size_t cue_count_{},control_count_{};
    std::array<std::array<std::uint64_t,3>,128> seen_{};
    std::optional<std::array<std::uint64_t,3>> event_floor_;
    std::array<std::uint64_t,32> slot_floor_{};
    std::array<bool,32> slot_known_{},slot_visible_{};
    std::array<std::optional<std::uint32_t>,32> slot_weapon_{};
    std::size_t next_seen_{};
    std::uint16_t fire_event_{},stop_event_{};
    std::uint32_t command_floor_{},fire_sequence_{5};
    std::uint64_t revision_{},life_{},restart_{},voice_lifetime_{},entity_ordinal_{},capacity_{},beam_frames_{};
    double now_{},ready_at_{},cooldown_{},fire_started_{};
    double last_command_seconds_{}; // command ordering, independent of presentation/RX time
    bool selected_{},eligible_{},confirmed_{},server_pair_{},require_release_{};
    std::uint64_t socket_resource_{};
    assets::AssetSourceFingerprint socket_revision_{};
    game_api::RemoteWeaponSocketPlan socket_{};
    std::uint64_t starts_{},replacements_{},stops_{};
    std::optional<client::RuntimeObservationSource> animation_source_;
    std::uint64_t animation_floor_{};
    bool animation_floor_pending_{true};
};
}

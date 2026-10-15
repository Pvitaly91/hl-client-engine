#pragma once
#include <hlclient/game_api/game_client_module.hpp>
namespace hlclient::games::halflife {
[[nodiscard]] bool gauss_sequences(const game_api::LocalWeaponModelMetadata&) noexcept;
// One session-owned HL presentation controller. No damage/health/ammo writes,
// transport, asset IO, clock, renderer or persistent engine pointer.
class GaussPresentation final {
public:
    void reset(game_api::GameSessionIdentity) noexcept;
    void configure(std::span<const game_api::ScriptedEventBinding>) noexcept;
    void maximum_clients(std::uint32_t n) noexcept {if(n>=1 && n<=32) {multiplayer_=n>1;multiplayer_known_=true;}}
    void bind(const std::optional<game_api::LocalWeaponModelMetadata>&);
    void observe(const client::RuntimeClientObservationState&,double) noexcept;
    std::optional<assets::AssetVector3> command(const game_api::LocalWeaponSubmittedCommand&) noexcept;
    void event(const game_api::CommittedScriptedEvent&) noexcept;
    bool handles(std::uint16_t index) const noexcept {return (fire_event_ && index==fire_event_) || (spin_event_ && index==spin_event_);}
    void poses(std::span<const game_api::RemoteWeaponPoseSample>) noexcept;
    game_api::RemoteWeaponSocketPlan socket_plan(const game_api::RemoteWeaponModelContext&) noexcept;
    void entities(std::span<const client::RuntimePacketEntityObservation>,std::uint64_t) noexcept;
    void slot_boundary(std::uint32_t) noexcept;
    void cancel(double now=0) noexcept;
    bool selected() const noexcept {return selected_;}
    game_api::LocalWeaponPresentationSnapshot sample(double) noexcept;
    game_api::LocalVisualFrame visuals(double) const noexcept;
    void append_audio(game_api::LocalAudioBatch&) noexcept;
    game_api::BeamEffectsFrame frame(const game_api::IStaticEffectTrace&,double,std::optional<assets::AssetVector3>) noexcept;
    static std::span<const game_api::LocalSoundReference> resources() noexcept;
private:
    using V=assets::AssetVector3;
    using Identity=game_api::LocalWeaponActionIdentity;
    enum class Discharge { primary, released, exhausted };
    struct Shot {Identity action;V origin,direction;float energy{};bool primary{};double at{};};
    void voice(game_api::LocalVoiceControl::Operation,std::uint32_t,float,double,std::optional<V> = {}) noexcept;
    void cue(std::string_view,double,float,float,std::optional<V> = {}) noexcept;
    std::optional<V> fire(const game_api::LocalWeaponSubmittedCommand&,Discharge,float) noexcept;
    void animate(std::uint32_t,double,game_api::LocalWeaponActionStatus=game_api::LocalWeaponActionStatus::predicted_pending) noexcept;
    void queue(Shot) noexcept;
    void sprite(const Shot&,V,V,bool,float,float,double,std::optional<V> = {},std::optional<V> = {},
        std::optional<game_api::EffectSurfaceAttachment> = {}) noexcept;
    game_api::GameSessionIdentity identity_{};
    std::optional<game_api::LocalWeaponModelMetadata> model_;
    bool multiplayer_{true},multiplayer_known_{},selected_{},eligible_{},charging_seen_{},charge_command_owned_{};
    unsigned water_{},charge_phase_{};int ammo_{},reservations_{};
    std::uint32_t command_floor_{};std::uint64_t revision_{},life_{},restart_{},serial_{};
    double observed_at_{},charge_started_{},burn_at_{},cooldown_{},idle_at_{},now_{},attack_ready_at_{};
    double completed_secondary_echo_until_{};
    std::uint64_t voice_lifetime_{};
    std::array<std::uint64_t,33> voice_id_{};
    std::uint16_t fire_event_{},spin_event_{};
    game_api::LocalWeaponPresentationSnapshot snapshot_{};
    std::array<std::optional<Shot>,16> shots_{};
    std::array<std::optional<game_api::BeamSpriteFrame>,128> sprites_{};
    std::array<game_api::LocalSoundCue,32> cues_{};std::size_t cue_count_{};
    std::array<game_api::LocalVoiceControl,32> controls_{};std::size_t control_count_{};
    std::array<std::array<std::uint64_t,3>,128> seen_{};std::size_t next_seen_{};
    std::array<std::optional<game_api::RemoteWeaponPoseSample>,32> poses_{};
    std::array<bool,32> remote_charging_{};
    std::array<std::uint64_t,32> remote_floor_{};
    std::array<double,32> remote_update_at_{};
    std::uint64_t entity_ordinal_{};
    std::optional<std::array<std::uint64_t,3>> event_floor_;
    std::uint64_t shot_count_{},reflections_{},penetrations_{},rejected_{},capacity_{};
    std::uint64_t socket_resource_{};
    assets::AssetSourceFingerprint socket_revision_{};
    game_api::RemoteWeaponSocketPlan socket_{};
};
}

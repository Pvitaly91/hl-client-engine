#pragma once
#include <hlclient/games/halflife/weapon_audio.hpp>
#include <hlclient/games/halflife/rpg_profile.hpp>
#include <hlclient/games/halflife/hornet_profile.hpp>
#include <hlclient/games/halflife/hand_grenade_profile.hpp>
#include <hlclient/games/halflife/satchel_profile.hpp>
#include <hlclient/games/halflife/tripmine_profile.hpp>
#include <hlclient/games/halflife/snark_profile.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/remote_effects.hpp>
#include <hlclient/client/runtime_observation.hpp>
namespace hlclient::games::halflife {
using namespace game_api;
class LocalWeaponPresentationController final {
public:
  void configure_maximum_clients(std::uint32_t value) noexcept { maximum_clients_=value; }
  void configure_projection(double horizontal) noexcept { crossbow_sniper_=maximum_clients_>1 && horizontal==20; }
  [[nodiscard]] bool crossbow_sniper_action() const noexcept { return action_ && action_->crossbow_sniper; }
  [[nodiscard]] bool hand_grenade_can_holster() const noexcept {
    return !hand_grenade_selected_ || (hand_grenade_.phase!=HandGrenadePhase::primed &&
        hand_grenade_.phase!=HandGrenadePhase::release_pending);
  }
  [[nodiscard]] const HandGrenadePresentationState& hand_grenade_state() const noexcept { return hand_grenade_; }
  [[nodiscard]] bool hand_grenade_waiting_retirement() const noexcept {
    return hand_grenade_selected_ && hand_grenade_.phase==HandGrenadePhase::retired;
  }
  void bind_model(std::optional<LocalWeaponModelMetadata>);
  void bind_model_variants(std::span<const LocalWeaponModelMetadata>);
  [[nodiscard]] const SatchelPresentationState& satchel_state() const noexcept { return satchel_; }
  [[nodiscard]] std::optional<LocalWeaponModelMetadata> satchel_presentation_model() const {
    return satchel_selected_ ? model_ : std::nullopt;
  }
  [[nodiscard]] bool satchel_waiting_retirement() const noexcept { return satchel_selected_ && satchel_.retired; }
  void observe(const client::RuntimeClientObservationState&, double now_seconds);
  void submit(const LocalWeaponSubmittedCommand&, double now_seconds);
  void prepare_presentation(const IStaticEffectTrace&,double,
      std::optional<std::uint32_t> receiving_entity) noexcept;
  [[nodiscard]] const TripminePresentationState& tripmine_state() const noexcept {return tripmine_;}
  [[nodiscard]] bool tripmine_waiting_retirement() const noexcept {return tripmine_selected_ && tripmine_.retired;}
  [[nodiscard]] const SnarkPresentationState& snark_state() const noexcept {return snark_;}
  [[nodiscard]] bool snark_waiting_retirement() const noexcept {return snark_selected_ && snark_.retired;}
  void configure_scripted_events(std::span<const ScriptedEventBinding>) noexcept;
  void committed_scripted_event(const CommittedScriptedEvent&,
      std::optional<std::uint32_t> receiving_entity) noexcept;
  // Local-compatible presentation only; never confirms a server melee hit.
  [[nodiscard]] bool resolve_crowbar_world_hit(const LocalWeaponActionIdentity&,
      double now_seconds) noexcept;
  void cancel_uncommitted() noexcept;
  [[nodiscard]] LocalWeaponPresentationSnapshot sample(double now_seconds) noexcept;
  // Inspect committed action provenance without advancing the presentation
  // clock or consuming audio/visual publication at an RX timestamp.
  [[nodiscard]] std::optional<LocalWeaponActionIdentity> current_action_identity() const noexcept;
  LocalAudioBatch drain_audio() noexcept { return audio_.drain(); }

private:
  WeaponAudio audio_;
  struct ActionState final {
    LocalWeaponActionIdentity identity{};
    LocalWeaponActionStatus status{LocalWeaponActionStatus::predicted_pending};
    std::size_t pre_record_ordinal{};
    std::optional<std::int32_t> clip_before;
    std::optional<std::uint8_t> reserve_before;
    double deadline{};
    LocalWeaponVisual visual{};
    // Glock secondary is the same neutral bullet-fire effect, not an M203
    // launch. Retain its HL pose/cycle distinction until server confirmation.
    bool glock_secondary{};
    // Presentation reservation only; canonical clip remains server-owned.
    std::uint8_t rounds_reserved{1U};
    bool scripted_event_confirmed{};
    bool crossbow_sniper{};
  };
  struct SeenEvent final {
    client::RuntimeObservationSource source{};
    std::uint32_t sequence{};
    std::uint8_t body{};
  };
  void finish_pending(LocalWeaponActionStatus) noexcept;
  void start(LocalWeaponAction, std::uint32_t command_sequence, double started_at,
             std::uint32_t sequence, std::uint8_t body, double deadline,
             bool glock_secondary=false, bool apply_recoil=true);
  [[nodiscard]] bool supported() const noexcept;
  [[nodiscard]] bool valid_pose(std::uint32_t, std::uint8_t) const noexcept;
  [[nodiscard]] bool advance_time(double) noexcept;
  void publish_idle(double);
  void observe_shotgun_reload(std::optional<std::int32_t> previous_stage,
      std::optional<std::int32_t> previous_clip, double now);
  void observe_empty_magazine_reload(std::optional<bool> previous_reload,
      bool client_pose_published, std::uint32_t sequence, double now);
  void start_crossbow(std::uint32_t command,double at,int post_clip,int reserve,bool sniper);
  void start_hornet(HornetFireMode,std::uint32_t command,double at,bool committed=false);
  void expire_hornet_reservations(double now) noexcept;
  void observe_hornet_ammo(std::optional<std::uint8_t> before,bool fresh,double now);
  void release_confirmed_hornet_reservations() noexcept;
  void committed_hornet_event(const CommittedScriptedEvent&,std::optional<std::uint32_t>) noexcept;
  void clear_hornet_state() noexcept;
  void clear_hand_grenade_state() noexcept;
  void submit_hand_grenade(const LocalWeaponSubmittedCommand&,bool ready);
  void observe_hand_grenade(const client::RuntimeClientObservationState&,bool fresh_client,
      std::optional<std::uint8_t> previous_reserve,bool fresh_ammo,
      std::optional<std::uint32_t> changed_client_animation,double now);
  void clear_satchel_state() noexcept;
  void clear_tripmine_state() noexcept;
  void clear_snark_state() noexcept;
  void submit_snark(const LocalWeaponSubmittedCommand&,bool);
  void enqueue_snark(const LocalWeaponSubmittedCommand&,bool,std::uint32_t) noexcept;
  void prepare_snark(const IStaticEffectTrace&,double,std::optional<std::uint32_t>) noexcept;
  void observe_snark(const client::RuntimeClientObservationState&,std::optional<std::uint8_t>,bool,
      std::optional<std::uint32_t>,double);
  void expire_snark_receipts(double) noexcept;
  void committed_snark_event(const CommittedScriptedEvent&,std::optional<std::uint32_t>) noexcept;
  void expire_tripmine_receipts(double) noexcept;
  void submit_tripmine(const LocalWeaponSubmittedCommand&,bool);
  void observe_tripmine(const client::RuntimeClientObservationState&,
      std::optional<std::uint8_t>,bool,double);
  void committed_tripmine_event(const CommittedScriptedEvent&,
      std::optional<std::uint32_t>) noexcept;
  [[nodiscard]] bool select_satchel_model(bool radio);
  void submit_satchel(const LocalWeaponSubmittedCommand&,bool ready,bool secondary_ready);
  void observe_satchel(const client::RuntimeClientObservationState&,bool fresh_client,
      std::optional<std::uint8_t> previous_reserve,bool fresh_ammo,
      std::optional<std::uint32_t> changed_client_animation,double now);
  std::optional<LocalWeaponModelMetadata> model_;
  std::uint64_t generation_{};
  std::optional<std::uint8_t> weapon_id_;
  std::optional<std::uint32_t> model_index_;
  std::size_t model_bound_record_ordinal_{};
  std::optional<std::int32_t> clip_;
  std::optional<std::uint8_t> reserve_;
  std::optional<std::uint8_t> secondary_reserve_;
  std::optional<std::uint32_t> water_level_;
  std::optional<bool> in_reload_;
  std::optional<double> next_primary_attack_;
  std::optional<double> next_secondary_attack_;
  std::optional<double> next_client_attack_, next_reload_;
  std::optional<std::int32_t> special_reload_state_;
  std::optional<double> pump_time_;
  bool canonical_dead_{};
  std::uint64_t life_epoch_{}, life_deaths_{};
  bool life_release_barrier_{};
  double timer_observed_at_{};
  std::optional<client::RuntimeObservationSource> client_source_, hud_source_;
  std::optional<std::uint32_t> client_animation_sequence_;
  std::optional<ActionState> action_;
  std::optional<LocalWeaponVisual> server_visual_;
  std::uint32_t idle_sequence_{};
  std::uint8_t idle_body_{};
  std::uint64_t canonical_revision_{}, restart_identity_{};
  std::array<std::optional<LocalWeaponSubmittedCommand>, 64U> command_history_{};
  std::array<std::optional<SeenEvent>, 16U> event_history_{};
  std::size_t event_history_next_{};
  std::uint32_t last_command_sequence_{}, crowbar_cycle_{};
  double last_primary_fire_at_{-1.0e9}, last_melee_swing_at_{-1.0e9};
  double glock_fire_cycle_seconds_{0.3};
  double last_secondary_launch_at_{-1.0e9};
  double launch_idle_not_before_{};
  std::optional<ActionState> pending_launch_;
  std::size_t provisional_grenades_{};
  std::optional<std::uint16_t> secondary_event_index_;
  std::optional<std::uint16_t> glock_secondary_event_index_;
  struct SeenLaunch { std::uint64_t record{}; std::uint32_t cursor{},ordinal{}; };
  std::array<SeenLaunch,32> launch_events_{};
  std::size_t launch_event_next_{};
  double recoil_started_at_{-1.0e9}, last_time_{};
  bool recoil_active_{}, server_punch_nonzero_{}, reload_button_held_{};
  bool mp5_selected_{};
  bool shotgun_selected_{};
  bool python_selected_{};
  bool crossbow_selected_{}, crossbow_sniper_{};
  bool rpg_selected_{};
  bool hornet_selected_{};
  bool hand_grenade_selected_{};
  bool satchel_selected_{};
  bool tripmine_selected_{};
  bool snark_selected_{};
  SnarkPresentationState snark_;
  std::optional<std::uint16_t> snark_event_index_;
  std::optional<client::RuntimeObservationSource> snark_ammo_source_;
  double snark_ready_at_{};
  std::optional<double> snark_recovery_at_;
  bool snark_release_required_{};
  bool snark_model_rebind_pending_{};
  struct SnarkAttempt {LocalWeaponSubmittedCommand command;bool event_only{};std::uint32_t event_ordinal{};};
  std::array<SnarkAttempt,8> snark_attempts_;
  std::size_t snark_attempt_count_{};
  struct SnarkReceipt {LocalWeaponActionIdentity identity;std::size_t pre_record{};double expires{};bool reserved{},confirmed{};};
  std::array<std::optional<SnarkReceipt>,8> snark_receipts_;
  std::size_t snark_receipt_next_{};
  struct SnarkEvent {CommittedScriptedEvent event;};
  std::array<std::optional<SnarkEvent>,16> snark_events_;
  std::size_t snark_event_next_{};
  TripminePresentationState tripmine_;
  std::optional<std::uint16_t> tripmine_event_index_;
  std::optional<client::RuntimeObservationSource> tripmine_ammo_source_;
  assets::AssetVector3 tripmine_view_offset_{};
  bool tripmine_view_offset_known_{},tripmine_release_required_{};
  double tripmine_ready_at_{};
  struct TripmineAttempt {LocalWeaponSubmittedCommand command;bool event_only{};std::uint32_t event_ordinal{};};
  std::optional<TripmineAttempt> tripmine_attempt_;
  struct TripmineReceipt {
    LocalWeaponActionIdentity identity;std::size_t pre_record{};double expires{};
    bool reserved{},confirmed{};
  };
  std::array<std::optional<TripmineReceipt>,8> tripmine_receipts_;
  std::size_t tripmine_receipt_next_{};
  struct TripmineEvent {std::uint64_t record;std::uint32_t cursor,ordinal;CommittedScriptedEvent event;};
  std::array<std::optional<TripmineEvent>,16> tripmine_events_;
  std::size_t tripmine_event_next_{};
  SatchelPresentationState satchel_;
  std::array<std::optional<LocalWeaponModelMetadata>,2> satchel_models_;
  std::optional<client::RuntimeObservationSource> satchel_ammo_source_;
  std::optional<double> satchel_idle_ready_at_;
  double satchel_primary_ready_at_{},satchel_secondary_ready_at_{},satchel_command_time_{};
  bool satchel_release_required_{};
  struct SatchelReceipt {
    LocalWeaponActionIdentity identity;
    std::size_t pre_record{};
    bool reserved{};
  };
  std::array<std::optional<SatchelReceipt>,5> satchel_receipts_;
  HandGrenadePresentationState hand_grenade_;
  std::optional<double> hand_grenade_idle_timer_;
  std::optional<client::RuntimeObservationSource> hand_grenade_ammo_source_;
  std::size_t hand_grenade_prime_record_{};
  bool hand_grenade_server_primed_{};
  bool hand_grenade_release_required_{};
  std::optional<std::uint8_t> hand_grenade_last_throw_reserve_;
  bool hand_grenade_last_reservation_pending_{};
  double hand_grenade_command_time_{};
  // Bounded visual-only reservations and echo receipts; no second ammo count
  // or recharge clock. Canonical AmmoX always owns the actual Hornets reserve.
  struct HornetReceipt {
    LocalWeaponActionIdentity identity{};
    std::size_t pre_record{};
    double deadline{};
    bool reserved{},event_confirmed{};
    LocalWeaponActionStatus status{LocalWeaponActionStatus::predicted_pending};
    std::optional<client::RuntimeObservationSource> confirmation_source;
  };
  std::array<std::optional<HornetReceipt>,32> hornet_receipts_{};
  std::size_t hornet_receipt_next_{};
  double hornet_primary_ready_at_{},hornet_secondary_ready_at_{};
  std::optional<std::uint16_t> hornet_event_index_;
  std::optional<client::RuntimeObservationSource> hornet_reserve_source_;
  RpgLauncherState rpg_state_;
  std::optional<std::uint16_t> rpg_event_index_;
  std::optional<std::uint16_t> crossbow_event_index_, crossbow_sniper_event_index_;
  std::uint32_t maximum_clients_{};
  std::optional<std::uint16_t> python_event_index_;
  double shotgun_fire_cycle_seconds_{0.75};
  std::optional<std::uint16_t> shotgun_single_event_index_, shotgun_double_event_index_;
  double recoil_impulse_{-2.0};
  // Same action/confirmation model, bounded outstanding automatic-fire window.
  std::array<std::optional<ActionState>,16> pending_fire_{};
  std::size_t next_pending_fire_{}, provisional_rounds_{};
  LocalWeaponPresentationSnapshot counters_{};
};
} // namespace hlclient::games::halflife

#pragma once
#include <hlclient/games/halflife/weapon_audio.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/client/runtime_observation.hpp>
namespace hlclient::games::halflife {
using namespace game_api;
class LocalWeaponPresentationController final {
public:
  void bind_model(std::optional<LocalWeaponModelMetadata>);
  void observe(const client::RuntimeClientObservationState&, double now_seconds);
  void submit(const LocalWeaponSubmittedCommand&, double now_seconds);
  // Local-compatible presentation only; never confirms a server melee hit.
  [[nodiscard]] bool resolve_crowbar_world_hit(const LocalWeaponActionIdentity&,
      double now_seconds) noexcept;
  void cancel_uncommitted() noexcept;
  [[nodiscard]] LocalWeaponPresentationSnapshot sample(double now_seconds) noexcept;
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
  };
  struct SeenEvent final {
    client::RuntimeObservationSource source{};
    std::uint32_t sequence{};
    std::uint8_t body{};
  };
  void finish_pending(LocalWeaponActionStatus) noexcept;
  void start(LocalWeaponAction, std::uint32_t command_sequence, double started_at,
             std::uint32_t sequence, std::uint8_t body, double deadline);
  [[nodiscard]] bool supported() const noexcept;
  [[nodiscard]] bool valid_pose(std::uint32_t, std::uint8_t) const noexcept;
  [[nodiscard]] bool advance_time(double) noexcept;
  void publish_idle(double);
  std::optional<LocalWeaponModelMetadata> model_;
  std::uint64_t generation_{};
  std::optional<std::uint8_t> weapon_id_;
  std::optional<std::uint32_t> model_index_;
  std::size_t model_bound_record_ordinal_{};
  std::optional<std::int32_t> clip_;
  std::optional<std::uint8_t> reserve_;
  std::optional<bool> in_reload_;
  std::optional<double> next_primary_attack_;
  std::optional<double> next_client_attack_, next_reload_;
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
  double recoil_started_at_{-1.0e9}, last_time_{};
  bool recoil_active_{}, server_punch_nonzero_{}, reload_button_held_{};
  LocalWeaponPresentationSnapshot counters_{};
};
} // namespace hlclient::games::halflife

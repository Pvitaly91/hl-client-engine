#pragma once
#include <hlclient/games/halflife/local_weapon_presentation.hpp>
#include <hlclient/game_api/game_client_module.hpp>

namespace hlclient::games::halflife {
// Session-owned Half-Life presentation rules. All inputs are borrowed only
// during a call; retained model/event values and output commands own storage.
class HalfLifePresentation final {
public:
  void bind_model(std::optional<game_api::LocalWeaponModelMetadata> model);
  void observe(const client::RuntimeClientObservationState&, double);
  void submit(const game_api::LocalWeaponSubmittedCommand&, double);
  [[nodiscard]] bool resolve_crowbar_world_hit(const game_api::LocalWeaponActionIdentity& action,
      double now) noexcept { return controller_.resolve_crowbar_world_hit(action,now); }
  void cancel_uncommitted() noexcept;
  [[nodiscard]] game_api::LocalWeaponPresentationSnapshot sample(double) noexcept;
  [[nodiscard]] game_api::ViewmodelIntent viewmodel(
      const client::RuntimeClientObservationState&,
      const std::optional<game_api::LocalWeaponModelMetadata>&, double,
      std::optional<game_api::LocalWeaponVisual> = {});
  [[nodiscard]] game_api::HudState hud(const client::RuntimeClientObservationState&, double);
  [[nodiscard]] game_api::CameraIntent camera(
      const client::RuntimeClientObservationState&, double local_punch_pitch_degrees) const noexcept;
  void reset() noexcept;
  game_api::LocalAudioBatch drain_audio() noexcept { return controller_.drain_audio(); }
  // Move committed owning events into a fixed ring without allocation. Their
  // lifetime starts once on the next finite monotonic presentation-clock call.
  void commit_pickups(game_api::GameRecordState&) noexcept;
private:
  struct PickupRow final {
    std::optional<game_api::GameRecordState::Pickup> event;
    std::optional<double> expires_at;
  };
  void advance_pickup_clock(double) noexcept;
  std::array<PickupRow, 4U> pickup_rows_;
  std::size_t pickup_next_{}, pickup_received_{};
  std::uint64_t pickup_revision_{}, pickup_life_{}, pickup_deaths_{};
  std::optional<double> pickup_clock_;
  LocalWeaponPresentationController controller_;
  std::uint64_t viewmodel_generation_{}, life_epoch_{}, life_deaths_{};
  std::optional<std::uint32_t> animated_model_index_, bound_viewmodel_index_;
  std::optional<std::uint8_t> bound_weapon_id_;
  std::optional<client::RuntimeObservationSource> animation_source_;
  std::optional<std::uint64_t> local_animation_restart_;
  std::size_t model_bound_record_ordinal_{};
  std::optional<std::uint32_t> animated_sequence_;
  std::uint8_t animated_body_{};
  double animation_started_at_{};
  std::uint64_t feedback_generation_{}, feedback_life_epoch_{}, feedback_revision_{};
  double feedback_until_{};
};
} // namespace hlclient::games::halflife

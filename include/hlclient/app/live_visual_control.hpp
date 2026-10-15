#pragma once

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/gameplay_input/gameplay_input_intent.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::app {

// Application completion and verification coverage are independent. The
// primary runtime/startup error takes precedence; a healthy manual close/limit does
// not require scripted movement/replay achievements.
struct LiveVisualSessionOutcome final {
  int exit_code{2};
  std::string_view application_result{"incomplete"};
  std::string_view primary_error{"application_incomplete"};
  std::string_view scripted_coverage{"not_evaluated"};
  std::string_view prediction_coverage{"limited"};
};
[[nodiscard]] LiveVisualSessionOutcome evaluate_live_visual_session(
    bool keyboard_mouse, bool completed, bool runtime_failed,
    bool scripted_verified, bool prediction_verified, bool reference_prediction,
    std::string_view runtime_error,
    std::optional<auth::AuthenticationErrorCode> authentication_error = {}) noexcept;
[[nodiscard]] std::string_view live_authentication_failure_reason(
    auth::AuthenticationErrorCode) noexcept;

enum class LiveVisualViewStatus : std::uint8_t {
  ready,
  observation_unavailable,
  generation_mismatch,
  receiving_client_unavailable,
  health_unavailable,
  receiving_client_not_alive,
  origin_unavailable,
  view_offset_unavailable,
  unsupported_view_offset_shape,
  invalid_local_angles,
  publication_rejected,
};

[[nodiscard]] std::string_view to_string(LiveVisualViewStatus) noexcept;

// Input activation follows at least one successfully presented frame whose
// retained world resources were uploaded and submitted. Optional entity
// visibility is evidence, not a prerequisite for turning or movement.
[[nodiscard]] bool live_visual_render_ready_for_input(
    std::uint64_t world_draws, std::uint64_t world_uploads,
    std::uint64_t successful_presentations) noexcept;

struct LiveVisualCameraSample final {
  std::uint64_t generation{0U};
  std::uint64_t publication_revision{0U};
  client::RuntimeObservationSource source{};
  std::optional<double> server_time_seconds;
  assets::AssetVector3 origin{};
  assets::AssetVector3 view_offset{};
  assets::AssetVector3 eye_position{};
  double local_yaw_degrees{0.0};
  double local_pitch_degrees{0.0};
  bool fresh_server_sample{false};
  bool vertical_only_view_offset_contract{false};
  bool server_angle_correction_applied{false};
  std::optional<assets::AssetVector3> server_punch_angle;
  // Observed composed camera, never fed back into movement/wire input.
  assets::AssetVector3 presented_angles_degrees{};
};

struct LiveVisualCameraUpdate final {
  LiveVisualViewStatus status{LiveVisualViewStatus::observation_unavailable};
  std::optional<LiveVisualCameraSample> sample;
  bool local_orientation_changed{false};
  bool camera_published{false};

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == LiveVisualViewStatus::ready && sample.has_value();
  }
};

struct LiveVisualPredictedView final {
  assets::AssetVector3 origin{};
  assets::AssetVector3 view_offset{};
};

// First live-view policy: receiving-client clientdata owns translation while
// project-local mouse look owns orientation. It intentionally performs no
// prediction, extrapolation, free-flight translation, entity-number lookup or
// camera-entity inference.
class LiveVisualCameraController final {
public:
  [[nodiscard]] bool apply_local_look(
      const gameplay_input::GameplayInputIntent &intent) noexcept;
  [[nodiscard]] LiveVisualCameraUpdate update(
      const client::RuntimeClientObservationState *observation,
      const gameplay_input::GameplayInputIntent &intent,
      client::ClientWorldState &target,
      const game_api::CameraIntent &camera_intent,
      bool apply_input_delta = true,
      std::optional<LiveVisualPredictedView> predicted_view = {},
      double viewport_aspect = 4.0 / 3.0,
      std::optional<double> presentation_seconds = {}) noexcept;

  [[nodiscard]] double yaw_degrees() const noexcept { return yaw_degrees_; }
  [[nodiscard]] double pitch_degrees() const noexcept { return pitch_degrees_; }
  [[nodiscard]] std::uint64_t camera_revision() const noexcept {
    return camera_revision_;
  }

private:
  double yaw_degrees_{0.0};
  double pitch_degrees_{0.0};
  std::uint64_t camera_revision_{0U};
  std::optional<client::RuntimeObservationSource> last_server_source_;
  std::optional<client::RuntimeObservationSource> last_angle_correction_source_;
  std::uint64_t last_angle_correction_generation_{0U};
  std::optional<client::RenderCameraState> last_camera_;
  std::uint64_t last_camera_generation_{}, last_camera_life_{};
  struct AuthoritativeBlend final {
    std::uint64_t generation{}, life{}, deaths{};
    client::RuntimeObservationSource source{};
    assets::AssetVector3 from{}, target{};
    double start{}, duration{}, last_sample_time{};
  };
  std::optional<AuthoritativeBlend> authoritative_blend_;
};

} // namespace hlclient::app

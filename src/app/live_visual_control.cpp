#include <hlclient/app/live_visual_control.hpp>

#include <hlclient/gameplay_camera/first_person_camera.hpp>

#include <cmath>
#include <limits>

namespace hlclient::app {
namespace {

[[nodiscard]] bool finite_vector(const assets::AssetVector3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

} // namespace

std::string_view to_string(const LiveVisualViewStatus status) noexcept {
  switch (status) {
  case LiveVisualViewStatus::ready:
    return "ready";
  case LiveVisualViewStatus::observation_unavailable:
    return "observation_unavailable";
  case LiveVisualViewStatus::generation_mismatch:
    return "generation_mismatch";
  case LiveVisualViewStatus::receiving_client_unavailable:
    return "receiving_client_unavailable";
  case LiveVisualViewStatus::health_unavailable:
    return "health_unavailable";
  case LiveVisualViewStatus::receiving_client_not_alive:
    return "receiving_client_not_alive";
  case LiveVisualViewStatus::origin_unavailable:
    return "origin_unavailable";
  case LiveVisualViewStatus::view_offset_unavailable:
    return "view_offset_unavailable";
  case LiveVisualViewStatus::unsupported_view_offset_shape:
    return "unsupported_view_offset_shape";
  case LiveVisualViewStatus::invalid_local_angles:
    return "invalid_local_angles";
  case LiveVisualViewStatus::publication_rejected:
    return "publication_rejected";
  }
  return "unknown";
}

bool live_visual_render_ready_for_input(
    const std::uint64_t world_draws, const std::uint64_t world_uploads,
    const std::uint64_t successful_presentations) noexcept {
  return world_draws > 0U && world_uploads > 0U &&
         successful_presentations > 0U;
}

bool LiveVisualCameraController::apply_local_look(
    const gameplay_input::GameplayInputIntent &intent) noexcept {
  const auto yaw = gameplay_camera::normalize_yaw_degrees(
      yaw_degrees_ + intent.look_delta_yaw_degrees());
  const auto pitch = gameplay_camera::clamp_pitch_degrees(
      pitch_degrees_ + intent.look_delta_pitch_degrees(), -89.0, 89.0);
  if (!yaw || !pitch)
    return false;
  yaw_degrees_ = *yaw;
  pitch_degrees_ = *pitch;
  return true;
}

LiveVisualCameraUpdate LiveVisualCameraController::update(
    const client::RuntimeClientObservationState *const observation,
    const gameplay_input::GameplayInputIntent &intent,
    client::ClientWorldState &target,
    const bool apply_input_delta,
    const std::optional<LiveVisualPredictedView> predicted_view) noexcept {
  LiveVisualCameraUpdate result;
  if (observation == nullptr) {
    return result;
  }
  if (observation->generation == 0U ||
      observation->client_metadata.generation != observation->generation) {
    result.status = LiveVisualViewStatus::generation_mismatch;
    return result;
  }
  if (!observation->receiving_client ||
      observation->client_metadata.freshness ==
          client::RuntimeObservationFreshness::unavailable ||
      !observation->client_metadata.source) {
    result.status = LiveVisualViewStatus::receiving_client_unavailable;
    return result;
  }
  const auto &receiving = *observation->receiving_client;
  if (!receiving.health) {
    result.status = LiveVisualViewStatus::health_unavailable;
    return result;
  }
  if (*receiving.health <= 0.0) {
    result.status = LiveVisualViewStatus::receiving_client_not_alive;
    return result;
  }
  if (!receiving.origin.complete()) {
    result.status = LiveVisualViewStatus::origin_unavailable;
    return result;
  }
  if (!receiving.view_offset.z) {
    result.status = LiveVisualViewStatus::view_offset_unavailable;
    return result;
  }
  const bool lateral_complete = receiving.view_offset.x.has_value() &&
                                receiving.view_offset.y.has_value();
  const bool lateral_absent = !receiving.view_offset.x.has_value() &&
                              !receiving.view_offset.y.has_value();
  if (!lateral_complete && !lateral_absent) {
    result.status = LiveVisualViewStatus::unsupported_view_offset_shape;
    return result;
  }

  const auto previous_yaw = yaw_degrees_;
  const auto previous_pitch = pitch_degrees_;
  if (apply_input_delta && !apply_local_look(intent)) {
    result.status = LiveVisualViewStatus::invalid_local_angles;
    return result;
  }
  bool server_angle_correction_applied = false;
  if (observation->view_angle_correction &&
      (!last_angle_correction_source_ ||
       *last_angle_correction_source_ !=
           observation->view_angle_correction->source)) {
    const auto corrected_yaw = gameplay_camera::normalize_yaw_degrees(
        observation->view_angle_correction->yaw_degrees);
    const auto corrected_pitch = gameplay_camera::clamp_pitch_degrees(
        observation->view_angle_correction->pitch_degrees, -89.0, 89.0);
    if (!corrected_yaw || !corrected_pitch) {
      result.status = LiveVisualViewStatus::invalid_local_angles;
      return result;
    }
    yaw_degrees_ = *corrected_yaw;
    pitch_degrees_ = *corrected_pitch;
    last_angle_correction_source_ =
        observation->view_angle_correction->source;
    server_angle_correction_applied = true;
  }
  result.local_orientation_changed =
      previous_yaw != yaw_degrees_ || previous_pitch != pitch_degrees_;

  const assets::AssetVector3 origin{
      static_cast<float>(*receiving.origin.x),
      static_cast<float>(*receiving.origin.y),
      static_cast<float>(*receiving.origin.z)};
  const assets::AssetVector3 view_offset{
      lateral_complete ? static_cast<float>(*receiving.view_offset.x) : 0.0F,
      lateral_complete ? static_cast<float>(*receiving.view_offset.y) : 0.0F,
      static_cast<float>(*receiving.view_offset.z)};
  const auto presented_origin = predicted_view ? predicted_view->origin : origin;
  const auto presented_offset = predicted_view
      ? predicted_view->view_offset : view_offset;
  const assets::AssetVector3 eye{
      presented_origin.x + presented_offset.x,
      presented_origin.y + presented_offset.y,
      presented_origin.z + presented_offset.z};
  const auto forward =
      gameplay_camera::forward_from_yaw_pitch(yaw_degrees_, pitch_degrees_);
  if (!forward || !finite_vector(origin) || !finite_vector(view_offset) ||
      !finite_vector(presented_origin) || !finite_vector(presented_offset) ||
      !finite_vector(eye)) {
    result.status = LiveVisualViewStatus::invalid_local_angles;
    return result;
  }

  client::RenderCameraState camera;
  camera.position = eye;
  camera.target = {eye.x + forward->x, eye.y + forward->y,
                   eye.z + forward->z};
  camera.up = gameplay_camera::world_up();
  const bool camera_changed = !last_camera_ || *last_camera_ != camera;
  if (camera_changed) {
    if (camera_revision_ == (std::numeric_limits<std::uint64_t>::max)()) {
      result.status = LiveVisualViewStatus::publication_rejected;
      return result;
    }
    ++camera_revision_;
    if (!target.set_interactive_camera(
            camera, client::InteractiveCameraMetadata{
                        intent.input_sequence(), camera_revision_,
                        client::InteractiveCameraMode::player_walk,
                        std::nullopt,
                        client::ControlledEntityCameraStatus::not_applicable})) {
      result.status = LiveVisualViewStatus::publication_rejected;
      return result;
    }
    last_camera_ = camera;
    result.camera_published = true;
  }

  const auto &source = *observation->client_metadata.source;
  const bool fresh =
      observation->client_metadata.freshness ==
          client::RuntimeObservationFreshness::observed_in_record &&
      (!last_server_source_ || *last_server_source_ != source);
  last_server_source_ = source;
  result.status = LiveVisualViewStatus::ready;
  result.sample = LiveVisualCameraSample{
      observation->generation,
      observation->publication_revision,
      source,
      observation->server_time_seconds,
      origin,
      view_offset,
      eye,
      yaw_degrees_,
      pitch_degrees_,
      fresh,
      lateral_absent,
      server_angle_correction_applied};
  return result;
}

} // namespace hlclient::app

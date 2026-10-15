#include <hlclient/app/live_visual_control.hpp>

#include <hlclient/gameplay_camera/first_person_camera.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hlclient::app {
namespace {

[[nodiscard]] bool finite_vector(const assets::AssetVector3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

} // namespace

std::string_view live_authentication_failure_reason(auth::AuthenticationErrorCode code) noexcept {
  switch(code) {
  case auth::AuthenticationErrorCode::unavailable: return "authentication_unavailable";
  case auth::AuthenticationErrorCode::configuration_error: return "authentication_configuration_error";
  case auth::AuthenticationErrorCode::provider_error: return "authentication_provider_error";
  case auth::AuthenticationErrorCode::invalid_material: return "authentication_invalid_material";
  case auth::AuthenticationErrorCode::material_too_large: return "authentication_material_too_large";
  case auth::AuthenticationErrorCode::timed_out: return "authentication_timed_out";
  case auth::AuthenticationErrorCode::cancelled: return "authentication_cancelled";
  }
  return "authentication_provider_error";
}

LiveVisualSessionOutcome evaluate_live_visual_session(
    bool keyboard_mouse, bool completed, bool runtime_failed,
    bool scripted_verified, bool prediction_verified, bool reference_prediction,
    std::string_view runtime_error,
    std::optional<auth::AuthenticationErrorCode> authentication_error) noexcept {
  LiveVisualSessionOutcome result;
  result.scripted_coverage = keyboard_mouse ? "not_evaluated" :
      scripted_verified ? "verified" : "limited";
  result.prediction_coverage = !reference_prediction ? "off" :
      prediction_verified ? "verified" : "limited";
  if (runtime_failed) {
    result.application_result = "error";
    result.primary_error = runtime_error.empty() ? "runtime_error" : runtime_error;
    return result;
  }
  if (authentication_error) {
    result.application_result = "error";
    result.primary_error = live_authentication_failure_reason(*authentication_error);
    return result;
  }
  if (completed) {
    result.exit_code = 0;
    result.application_result = "completed";
    result.primary_error = "none";
  }
  return result;
}

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
    const game_api::CameraIntent &camera_intent,
    const bool apply_input_delta,
    const std::optional<LiveVisualPredictedView> predicted_view,
    const double viewport_aspect, const std::optional<double> presentation_seconds) noexcept {
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
  if (camera_intent.status == game_api::CameraIntentStatus::health_unavailable) {
    result.status = LiveVisualViewStatus::health_unavailable;
    return result;
  }
  if (camera_intent.status == game_api::CameraIntentStatus::receiving_client_not_alive) {
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
      (last_angle_correction_generation_ != observation->generation ||
       !last_angle_correction_source_ ||
       *last_angle_correction_source_ !=
           observation->view_angle_correction->source)) {
    const auto corrected_yaw = gameplay_camera::normalize_yaw_degrees(
        observation->view_angle_correction->yaw_degrees);
    const auto corrected_pitch = gameplay_camera::clamp_pitch_degrees(
        // svc_setangle retains GoldSrc turns (including e.g. 350 = -10).
        // Normalize before changing convention and clamping the camera.
        -std::remainder(observation->view_angle_correction->pitch_degrees, 360.0),
        -89.0, 89.0);
    if (!corrected_yaw || !corrected_pitch) {
      result.status = LiveVisualViewStatus::invalid_local_angles;
      return result;
    }
    yaw_degrees_ = *corrected_yaw;
    pitch_degrees_ = *corrected_pitch;
    last_angle_correction_source_ =
        observation->view_angle_correction->source;
    last_angle_correction_generation_ = observation->generation;
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
  const bool prediction_allowed = predicted_view && camera_intent.allow_predicted_translation;
  const auto presented_origin = prediction_allowed ? predicted_view->origin : origin;
  const auto presented_offset = prediction_allowed
      ? predicted_view->view_offset : view_offset;
  assets::AssetVector3 eye{
      presented_origin.x + presented_offset.x,
      presented_origin.y + presented_offset.y,
      presented_origin.z + presented_offset.z};
  const auto blend_duration=camera_intent.authoritative_translation_blend_seconds;
  if (!std::isfinite(blend_duration) || blend_duration<0.0 || blend_duration>0.25) {
    result.status=LiveVisualViewStatus::publication_rejected; return result;
  }
  if (!prediction_allowed && blend_duration>0.0 && presentation_seconds &&
      std::isfinite(*presentation_seconds) && *presentation_seconds>=0.0) {
    const auto at=*presentation_seconds;
    const auto& source=*observation->client_metadata.source;
    const auto sample=[](const AuthoritativeBlend& blend,double time) {
      const double alpha=std::clamp((time-blend.start)/blend.duration,0.0,1.0);
      return assets::AssetVector3{static_cast<float>(blend.from.x+(blend.target.x-blend.from.x)*alpha),
          static_cast<float>(blend.from.y+(blend.target.y-blend.from.y)*alpha),
          static_cast<float>(blend.from.z+(blend.target.z-blend.from.z)*alpha)};
    };
    const bool same_boundary=authoritative_blend_ &&
        authoritative_blend_->generation==observation->generation &&
        authoritative_blend_->life==observation->lifecycle.life_epoch &&
        authoritative_blend_->deaths==observation->lifecycle.deaths &&
        at>=authoritative_blend_->last_sample_time;
    const bool new_sample=!same_boundary || (source!=authoritative_blend_->source &&
        source.record_ordinal>authoritative_blend_->source.record_ordinal);
    if (new_sample) {
      auto from=eye;
      if (same_boundary) from=sample(*authoritative_blend_,at);
      else if (!authoritative_blend_ && last_camera_ && last_camera_generation_==observation->generation &&
          last_camera_life_==observation->lifecycle.life_epoch) from=last_camera_->position;
      const double dx=eye.x-from.x,dy=eye.y-from.y,dz=eye.z-from.z;
      if (dx*dx+dy*dy+dz*dz>128.0*128.0) from=eye; // teleport: never drag a prior camera across the map
      authoritative_blend_=AuthoritativeBlend{observation->generation,observation->lifecycle.life_epoch,
          observation->lifecycle.deaths,source,from,eye,at,blend_duration,at};
    }
    authoritative_blend_->last_sample_time=at;
    eye=sample(*authoritative_blend_,at);
  } else authoritative_blend_.reset();
  std::optional<assets::AssetVector3> punch;
  if (camera_intent.server_punch_angle)
    punch = assets::AssetVector3{(*camera_intent.server_punch_angle)[0],
        (*camera_intent.server_punch_angle)[1],(*camera_intent.server_punch_angle)[2]};
  if (camera_intent.status == game_api::CameraIntentStatus::invalid_local_punch ||
      !std::isfinite(camera_intent.pitch_offset_degrees) ||
      !std::isfinite(camera_intent.local_pitch_offset_degrees) ||
      !std::isfinite(camera_intent.yaw_offset_degrees) || !std::isfinite(camera_intent.roll_degrees))
    return result;
  const auto presented_pitch = std::clamp(
      pitch_degrees_ + camera_intent.pitch_offset_degrees +
          camera_intent.local_pitch_offset_degrees, -89.0, 89.0);
  const auto presented_yaw = yaw_degrees_ + camera_intent.yaw_offset_degrees;
  const auto forward = gameplay_camera::forward_from_yaw_pitch(
      presented_yaw, presented_pitch);
  if (!forward || !finite_vector(origin) || !finite_vector(view_offset) ||
      !finite_vector(presented_origin) || !finite_vector(presented_offset) ||
      !finite_vector(eye)) {
    result.status = LiveVisualViewStatus::invalid_local_angles;
    return result;
  }

  client::RenderCameraState camera;
  if(camera_intent.horizontal_field_of_view_degrees) {
    const auto degrees=*camera_intent.horizontal_field_of_view_degrees;
    if(!std::isfinite(degrees) || degrees<1.0 || degrees>179.0 ||
        !std::isfinite(viewport_aspect) || viewport_aspect<=0.0) {
      result.status=LiveVisualViewStatus::invalid_local_angles;return result;
    }
    constexpr double radians=0.01745329251994329577;
    camera.vertical_field_of_view_radians=static_cast<float>(
        2.0*std::atan(std::tan(degrees*radians*0.5)/viewport_aspect));
  }
  camera.position = eye;
  camera.target = {eye.x + forward->x, eye.y + forward->y,
                   eye.z + forward->z};
  camera.up = gameplay_camera::world_up();
  if (camera_intent.roll_degrees != 0.0 && forward) {
    constexpr double radians = 0.01745329251994329577;
    const auto roll = camera_intent.roll_degrees * radians;
    const auto right_x = forward->y;
    const auto right_y = -forward->x;
    const auto horizontal = std::hypot(right_x, right_y);
    if (horizontal > 1.0e-6F) {
      camera.up = {static_cast<float>(std::sin(roll) * right_x / horizontal),
                   static_cast<float>(std::sin(roll) * right_y / horizontal),
                   static_cast<float>(std::cos(roll))};
    }
  }
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
  last_camera_generation_=observation->generation;
  last_camera_life_=observation->lifecycle.life_epoch;
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
  result.sample->server_punch_angle = punch;
  result.sample->presented_angles_degrees={static_cast<float>(presented_pitch),
      static_cast<float>(presented_yaw),static_cast<float>(camera_intent.roll_degrees)};
  return result;
}

} // namespace hlclient::app

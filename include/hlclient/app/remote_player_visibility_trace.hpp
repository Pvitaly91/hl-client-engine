#pragma once

#include <hlclient/app/runtime_replay_local_assets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace hlclient::app {

// Diagnostic categories only. None of these alter source state, lighting,
// visibility, material selection or draw submission. "Queued" is not a GPU
// pixel observation and cannot establish that the user saw the model.
enum class RemotePlayerVisibilityStage : std::uint8_t {
  source_absent, not_visual, model_not_advertised, server_hidden128,
  unsupported_render_mode, game_not_ready, pose_unavailable, frame_unavailable,
  pvs_culled, frustum_culled, queued_visible, queued_unlit, queued_dim
};
[[nodiscard]] constexpr std::string_view to_string(RemotePlayerVisibilityStage stage) noexcept {
  switch (stage) {
  case RemotePlayerVisibilityStage::source_absent: return "source_absent";
  case RemotePlayerVisibilityStage::not_visual: return "not_visual";
  case RemotePlayerVisibilityStage::model_not_advertised: return "model_not_advertised";
  case RemotePlayerVisibilityStage::server_hidden128: return "server_hidden128";
  case RemotePlayerVisibilityStage::unsupported_render_mode: return "unsupported_render_mode";
  case RemotePlayerVisibilityStage::game_not_ready: return "game_not_ready";
  case RemotePlayerVisibilityStage::pose_unavailable: return "pose_unavailable";
  case RemotePlayerVisibilityStage::frame_unavailable: return "frame_unavailable";
  case RemotePlayerVisibilityStage::pvs_culled: return "pvs_culled";
  case RemotePlayerVisibilityStage::frustum_culled: return "frustum_culled";
  case RemotePlayerVisibilityStage::queued_visible: return "queued_visible";
  case RemotePlayerVisibilityStage::queued_unlit: return "queued_unlit";
  case RemotePlayerVisibilityStage::queued_dim: return "queued_dim";
  }
  return "invalid";
}

// Entirely owned numeric evidence. No retained observation/frame pointers,
// names, native paths, buffers, resource handles or second state publication.
struct RemotePlayerVisibilitySample final {
  std::uint32_t entity{};
  std::uint64_t generation{}, publication_revision{}, source_identity{};
  std::size_t source_ordinal{};
  std::optional<double> server_seconds;
  std::optional<std::uint32_t> model_slot, effects, render_mode;
  game_api::RemotePlayerPresentationStatus game_status{game_api::RemotePlayerPresentationStatus::silent};
  bool source_available{}, model_advertised{}, game_ready{}, pose_ready{}, frame_available{};
  std::optional<std::array<double,3U>> entity_origin;
  std::optional<assets::WorldBounds> posed_bounds;
  std::optional<std::array<float,3U>> static_light_rgb;
  assets::AssetVector3 camera_position{}, camera_target{};
  float near_plane{0.1F};
  std::optional<double> camera_distance;
  RemotePlayerVisibilityStage stage{RemotePlayerVisibilityStage::source_absent};
};

[[nodiscard]] inline bool valid_remote_player_visibility_sample(
    const RemotePlayerVisibilitySample& sample) noexcept {
  const auto finite3=[](const assets::AssetVector3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  if (!sample.entity || sample.entity>game_api::kMaximumRemotePlayers || !sample.generation ||
      static_cast<unsigned>(sample.stage)>static_cast<unsigned>(RemotePlayerVisibilityStage::queued_dim) ||
      static_cast<unsigned>(sample.game_status)>static_cast<unsigned>(game_api::RemotePlayerPresentationStatus::stale_source) ||
      !finite3(sample.camera_position) || !finite3(sample.camera_target) ||
      !std::isfinite(sample.near_plane) || sample.near_plane<=0.0F ||
      (sample.server_seconds && !std::isfinite(*sample.server_seconds)) ||
      (sample.camera_distance && (!std::isfinite(*sample.camera_distance) || *sample.camera_distance<0.0))) return false;
  if (sample.entity_origin && !std::ranges::all_of(*sample.entity_origin,
      [](double component) { return std::isfinite(component); })) return false;
  if (sample.static_light_rgb && !std::ranges::all_of(*sample.static_light_rgb,
      [](float component) { return std::isfinite(component) && component>=0.0F && component<=1.0F; })) return false;
  if (sample.posed_bounds) {
    const auto& b=*sample.posed_bounds;
    if (!finite3(b.minimum) || !finite3(b.maximum) || b.minimum.x>b.maximum.x ||
        b.minimum.y>b.maximum.y || b.minimum.z>b.maximum.z) return false;
  }
  return true;
}

// The caller supplies one coherent committed observation, its current visual
// summary and current immutable frame. Source absence wins over stale frame
// contents. Reads are bounded by existing observation/frame limits; output
// borrows nothing, and construction performs no allocation, I/O or clock read.
[[nodiscard]] inline std::optional<RemotePlayerVisibilitySample>
build_remote_player_visibility_sample(const std::uint32_t entity,
    const client::RuntimeClientObservationState& observation,
    const ReplayLocalVisualSummary& summary, const entity_render::EntityRenderFrame* frame,
    const renderer::RenderCamera& camera) noexcept {
  if (!entity || entity>game_api::kMaximumRemotePlayers ||
      observation.packet_entities.size()>client::kMaximumRuntimeObservationEntities ||
      summary.remote_player_count>summary.remote_players.size()) return {};
  RemotePlayerVisibilitySample result;
  result.entity=entity; result.generation=observation.generation;
  result.publication_revision=observation.publication_revision;
  result.server_seconds=observation.server_time_seconds;
  result.camera_position=camera.position; result.camera_target=camera.target; result.near_plane=camera.near_plane;
  if (observation.entity_metadata.source) {
    result.source_identity=observation.entity_metadata.source->record_identity;
    result.source_ordinal=observation.entity_metadata.source->record_ordinal;
  }
  const auto source=std::ranges::find(observation.packet_entities,entity,
      &client::RuntimePacketEntityObservation::entity_number);
  if (source!=observation.packet_entities.end()) {
    result.source_available=true; result.model_slot=source->model_index;
    result.effects=source->effects; result.render_mode=source->render_mode;
    if (source->origin.complete()) result.entity_origin=std::array{*source->origin.x,*source->origin.y,*source->origin.z};
    result.model_advertised=result.model_slot && *result.model_slot && summary.model_names.contains(*result.model_slot);
    for (std::size_t i=0U;i<summary.remote_player_count;++i) {
      const auto& diagnostic=summary.remote_players[i];
      if (diagnostic.entity!=entity) continue;
      result.game_status=diagnostic.status;
      result.game_ready=diagnostic.status==game_api::RemotePlayerPresentationStatus::ready;
      result.pose_ready=diagnostic.pose_submitted;
      result.static_light_rgb=diagnostic.static_light;
      break;
    }
    const entity_render::StudioEntityRenderInstance* instance=nullptr;
    if (frame) {
      const auto found=std::ranges::find(frame->studio_instances(),entity,
          &entity_render::StudioEntityRenderInstance::entity_number);
      if (found!=frame->studio_instances().end()) {
        instance=&*found; result.frame_available=true; result.posed_bounds=instance->interpolated_bounds;
        result.entity_origin=std::array<double,3U>{instance->transform.origin.x,
            instance->transform.origin.y,instance->transform.origin.z};
        result.static_light_rgb=instance->static_light_rgb;
      }
    }
    using Stage=RemotePlayerVisibilityStage;
    if (!source->ordinary_visual_schema || !source->player_movement_schema) result.stage=Stage::not_visual;
    else if (!result.model_advertised) result.stage=Stage::model_not_advertised;
    else if ((source->effects.value_or(0U)&128U)!=0U) result.stage=Stage::server_hidden128;
    else if (source->render_mode.value_or(0U)!=0U) result.stage=Stage::unsupported_render_mode;
    else if (!result.game_ready) result.stage=Stage::game_not_ready;
    else if (!result.pose_ready) result.stage=Stage::pose_unavailable;
    else if (!instance) result.stage=Stage::frame_unavailable;
    else if (instance->visibility_status==entity_render::RuntimeEntityVisibilityStatus::culled_by_pvs)
      result.stage=Stage::pvs_culled;
    else if (instance->visibility_status==entity_render::RuntimeEntityVisibilityStatus::culled_by_frustum)
      result.stage=Stage::frustum_culled;
    else if (instance->visibility_status!=entity_render::RuntimeEntityVisibilityStatus::visible ||
        !std::ranges::any_of(frame->draw_commands(),[entity](const auto& command) {
          return command.entity_number==entity && command.visual_kind==entity_render::RuntimeEntityVisualKind::studio_model;
        })) result.stage=Stage::frame_unavailable;
    else {
      result.stage=Stage::queued_visible;
      if (result.static_light_rgb) {
        const auto maximum=*std::ranges::max_element(*result.static_light_rgb);
        if (maximum<=0.01F) result.stage=Stage::queued_unlit;
        else if (maximum<0.12F) result.stage=Stage::queued_dim;
      }
    }
  }
  if (result.entity_origin) result.camera_distance=std::hypot(
      (*result.entity_origin)[0]-camera.position.x,(*result.entity_origin)[1]-camera.position.y,
      (*result.entity_origin)[2]-camera.position.z);
  return valid_remote_player_visibility_sample(result) ? std::optional{result} : std::nullopt;
}

// Diagnostic distance bands only, never near-plane/culling thresholds. This
// records a close approach even if the CPU continues to queue the same model.
[[nodiscard]] constexpr std::uint8_t remote_player_visibility_distance_band(
    const RemotePlayerVisibilitySample& sample) noexcept {
  if (!sample.camera_distance) return 0U;
  return *sample.camera_distance<=48.0 ? 1U : *sample.camera_distance<=96.0 ? 2U : 3U;
}

// Fixed storage: 32 slot states and the LAST16 chronological changes. New
// generation clears the previous journal; a never-seen absent slot is silent.
// Stable movement/records/light values within one category/distance band do
// not create per-frame spam. All retained samples remain independent values.
class RemotePlayerVisibilityJournal final {
public:
  [[nodiscard]] bool observe(const RemotePlayerVisibilitySample& sample) noexcept {
    if (!valid_remote_player_visibility_sample(sample)) return false;
    if (generation_!=sample.generation) { reset(); generation_=sample.generation; }
    auto& slot=slots_[sample.entity-1U];
    if (!slot.seen && sample.stage==RemotePlayerVisibilityStage::source_absent) return false;
    const auto band=remote_player_visibility_distance_band(sample);
    if (slot.seen && slot.stage==sample.stage && slot.model==sample.model_slot &&
        slot.game_status==sample.game_status && slot.distance_band==band)
      return false;
    slot={true,sample.stage,sample.model_slot,sample.game_status,band};
    ++total_;
    if (count_==events_.size()) {
      std::move(events_.begin()+1U,events_.end(),events_.begin());
      --count_; ++dropped_;
    }
    events_[count_++]=sample;
    return true;
  }
  void reset() noexcept { slots_={}; events_={}; count_=0U; total_=0U; dropped_=0U; generation_=0U; }
  [[nodiscard]] std::span<const RemotePlayerVisibilitySample> events() const noexcept {
    return std::span{events_}.first(count_);
  }
  [[nodiscard]] std::uint64_t total() const noexcept { return total_; }
  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }
private:
  struct Slot {
    bool seen{};
    RemotePlayerVisibilityStage stage{};
    std::optional<std::uint32_t> model;
    game_api::RemotePlayerPresentationStatus game_status{};
    std::uint8_t distance_band{};
  };
  std::array<Slot,game_api::kMaximumRemotePlayers> slots_{};
  std::array<RemotePlayerVisibilitySample,16U> events_{};
  std::size_t count_{};
  std::uint64_t generation_{}, total_{}, dropped_{};
};

} // namespace hlclient::app

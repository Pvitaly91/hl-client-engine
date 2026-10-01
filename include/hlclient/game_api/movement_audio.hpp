#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::game_api {

// Borrowed texture name exists only for this call. The host supplies one
// completed, quantized simulation command, never a render sample or raw input.
// Missing texture and invalid support are distinct; neither is invented as a
// server-confirmed concrete material.
enum class MovementSurfaceStatus : std::uint8_t {
  found, unmapped_face, invalid_support, geometry_unavailable,
  ambiguous_support, source_texture_unavailable
};
enum class MovementAudioMode : std::uint8_t { ground, airborne, ladder, unsupported };
struct MovementAudioObservation {
  std::uint64_t generation{}, life_epoch{};
  std::uint32_t command_sequence{};
  std::uint16_t command_milliseconds{};
  double scheduled_seconds{}; // sampled command clock relative to host audio origin
  MovementAudioMode before_mode{MovementAudioMode::unsupported};
  MovementAudioMode after_mode{MovementAudioMode::unsupported};
  float horizontal_speed{}, total_speed{}, before_vertical_velocity{};
  bool ducked{}, before_grounded{}, after_grounded{};
  bool dry_context{}, jump_edge{};
  std::optional<bool> movevars_footsteps;
  MovementSurfaceStatus surface_status{MovementSurfaceStatus::geometry_unavailable};
  std::string_view texture_name;
  // Neutral source metadata, not collision-plane ordinals or game materials.
  std::optional<std::uint32_t> source_surface_index, source_material_index,
      source_texture_index, source_model_index;
  std::optional<assets::AssetVector3> world_origin;
};

} // namespace hlclient::game_api

#pragma once
#include <hlclient/assets/model_asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::game_api {

// Versioned visual-only policies. No SDK object, network handle or gameplay
// result crosses this boundary.
enum class LocalWeaponAction : std::uint8_t { primary_fire, reload, melee_swing };
enum class LocalWeaponActionStatus : std::uint8_t {
  predicted_pending, server_confirmed, server_corrected, server_rejected,
  timed_out, retained_server_event
};
enum class LocalWeaponAnimationSource : std::uint8_t {
  provisional_command, confirmed_weapon_state, clientdata_state, service_event
};
enum class LocalWeaponPresentationError : std::uint8_t {
  none, unsupported_model, unsupported_pose, conflicting_command,
  conflicting_event, stale_observation, invalid_time
};

struct LocalWeaponSequenceMetadata final {
  double fps{};
  std::uint32_t frame_count{};
  bool looping{};
  std::vector<assets::ModelSequenceEvent> events;
  friend bool operator==(const LocalWeaponSequenceMetadata&,
                         const LocalWeaponSequenceMetadata&) = default;
};
struct LocalWeaponModelMetadata final {
  std::uint64_t generation{};
  std::uint32_t model_index{};
  std::uint64_t resource_revision{};
  std::string resource_name;
  std::vector<LocalWeaponSequenceMetadata> sequences;
  // Computed from actual Studio body selection, not a sequence-number guess.
  std::array<bool, 256U> supported_bodies{};
  // Keep valid Studio body selection separate from renderer material support.
  std::array<bool, 256U> selectable_bodies{};
  friend bool operator==(const LocalWeaponModelMetadata&,
                         const LocalWeaponModelMetadata&) = default;
};
struct LocalWeaponSubmittedCommand final {
  std::uint64_t generation{};
  std::uint32_t sequence{};
  std::uint16_t buttons{};
  double sample_end_seconds{};
  // Captured once when the exact immutable wire command is newly submitted.
  // Optional because an initial camera/receiving-client may be unavailable.
  struct ShotContext final {
    assets::AssetVector3 eye{}, direction{};
    friend bool operator==(const ShotContext& a,const ShotContext& b) noexcept {
      return a.eye.x==b.eye.x && a.eye.y==b.eye.y && a.eye.z==b.eye.z &&
          a.direction.x==b.direction.x && a.direction.y==b.direction.y &&
          a.direction.z==b.direction.z;
    }
  };
  std::optional<ShotContext> shot_context;
  friend bool operator==(const LocalWeaponSubmittedCommand&,
                         const LocalWeaponSubmittedCommand&) = default;
};
struct LocalWeaponActionIdentity final {
  std::uint64_t generation{};
  std::uint8_t weapon_id{};
  std::uint32_t model_index{};
  std::uint64_t resource_revision{};
  std::uint32_t command_sequence{};
  LocalWeaponAction kind{};
  double started_at_seconds{};
  std::uint64_t canonical_pre_revision{};
  // Zero for the receiving player's command path; nonzero identifies an
  // independent committed remote emitter in shared bounded effect pools.
  std::uint32_t emitter_entity{};
  friend bool operator==(const LocalWeaponActionIdentity&, const LocalWeaponActionIdentity&) = default;
};
struct LocalWeaponVisual final {
  std::uint32_t sequence{};
  std::uint8_t body{};
  std::uint64_t restart_identity{};
  double started_at_seconds{};
  LocalWeaponAnimationSource source{LocalWeaponAnimationSource::provisional_command};
  LocalWeaponActionStatus status{LocalWeaponActionStatus::predicted_pending};
};
struct LocalWeaponPresentationSnapshot final {
  std::optional<LocalWeaponVisual> visual;
  std::optional<LocalWeaponAction> action;
  std::optional<LocalWeaponActionIdentity> identity;
  LocalWeaponActionStatus status{LocalWeaponActionStatus::retained_server_event};
  LocalWeaponPresentationError error{LocalWeaponPresentationError::none};
  double frame_coordinate{};
  bool animation_completed{};
  double local_punch_pitch_degrees{};
  double maximum_visual_recoil{};
  std::size_t actions_started{}, actions_confirmed{}, actions_rejected{}, actions_corrected{};
  std::size_t duplicate_submissions{}, duplicate_events{}, conflicting_duplicates{};
  std::size_t primary_fire_starts{}, reload_starts{}, melee_swing_starts{};
  std::size_t primary_fire_confirmed{}, reload_confirmed{}, melee_swing_confirmed{};
  std::size_t recoil_starts{}, recoil_confirmed{}, recoil_rejected{};
  // A swing timer confirms an action, never a hit/damage outcome.
  bool hit_status_available{};
};


enum class ViewmodelStatus : std::uint8_t {
  hidden, ready, unsupported_asset, unsupported_pose
};
enum class CameraIntentStatus : std::uint8_t {
  ready, health_unavailable, receiving_client_not_alive, invalid_local_punch
};
struct CameraIntent final {
  CameraIntentStatus status{CameraIntentStatus::ready};
  bool allow_predicted_translation{true};
  std::optional<std::array<float, 3U>> server_punch_angle;
  double pitch_offset_degrees{}, local_pitch_offset_degrees{};
  double yaw_offset_degrees{}, roll_degrees{};
};
// Owning, renderer-neutral pose request. No asset pointers survive a call.
struct ViewmodelIntent final {
  ViewmodelStatus status{ViewmodelStatus::hidden};
  std::uint64_t generation{};
  std::uint64_t publication_revision{};
  std::uint32_t model_index{};
  std::uint32_t sequence{};
  std::uint8_t body{};
  double frame_coordinate{};
  double local_time_seconds{};
  friend bool operator==(const ViewmodelIntent&, const ViewmodelIntent&) = default;
};
struct HudRectangle final {
  float x{}, y{}, width{}, height{};
  std::array<float, 4U> color{};
  friend bool operator==(const HudRectangle&, const HudRectangle&) = default;
};
struct HudText final {
  std::string text;
  float x{}, y{}, pixel_size{}, advance{}, line_height{};
  std::array<float, 4U> color{};
  friend bool operator==(const HudText&, const HudText&) = default;
};
// Bounded primitives: at most 8 rectangles and 8 texts, 160 bytes per text.
// Coordinates use top-left pixel space. Renderer supplies only glyph geometry.
struct HudDrawCommands final {
  std::vector<HudRectangle> rectangles;
  std::vector<HudText> texts;
  friend bool operator==(const HudDrawCommands&, const HudDrawCommands&) = default;
};
struct HudState final {
  HudDrawCommands draw;
  // Read-only diagnostics. Rendering uses exclusively the above primitives.
  std::optional<int> health;
  std::optional<int> armor;
  std::string weapon_name;
  std::optional<int> clip;
  std::optional<int> primary_reserve;
  std::string lifecycle_indicator;
  // Owning bounded feedback/telemetry; no counter changes are implied.
  std::vector<std::string> inventory_feedback;
  std::uint64_t inventory_feedback_revision{};
  std::size_t inventory_notifications_received{};
};
} // namespace hlclient::game_api

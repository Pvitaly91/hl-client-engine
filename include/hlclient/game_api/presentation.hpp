#pragma once
#include <hlclient/assets/model_asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

namespace hlclient::game_api {

// Presentation only: never fed to command aim, movement, collision or audio.
struct ScreenEffectsFrame final {
  std::array<float,3> translation{};
  double roll_degrees{};
  std::array<float,4> overlay_rgba{};
  bool modulate{};
};

// Versioned visual-only policies. No SDK object, network handle or gameplay
// result crosses this boundary.
enum class LocalWeaponAction : std::uint8_t { primary_fire, reload, melee_swing, secondary_launch, secondary_fire, prime, throw_release, deploy_charge, remote_detonate };
enum class LocalWeaponActionStatus : std::uint8_t {
  predicted_pending, server_confirmed, server_corrected, server_rejected,
  timed_out, retained_server_event
};
enum class LocalWeaponAnimationSource : std::uint8_t {
  provisional_command, confirmed_weapon_state, clientdata_state, service_event
};
enum class LocalWeaponPresentationError : std::uint8_t {
  none, unsupported_model, unsupported_pose, conflicting_command,
  conflicting_event, stale_observation, invalid_time, unsupported_event
};

struct LocalWeaponSequenceMetadata final {
  double fps{};
  std::uint32_t frame_count{};
  bool looping{};
  std::vector<assets::ModelSequenceEvent> events;
  std::string label; // owning imported metadata; semantic validation is game-owned
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
// Immutable, owning metadata from an approved model resource. Safe to retain
// across a local-asset owner change; the generation/revision stay in the value.
using LocalWeaponModelHandle = std::shared_ptr<const LocalWeaponModelMetadata>;
struct LocalWeaponSubmittedCommand final {
  std::uint64_t generation{};
  std::uint32_t sequence{};
  std::uint16_t buttons{};
  double sample_end_seconds{};
  // Captured once when the exact immutable wire command is newly submitted.
  // Optional because an initial camera/receiving-client may be unavailable.
  struct ShotContext final {
    assets::AssetVector3 eye{}, direction{}, right{0,-1,0}, up{0,0,1};
    // Owning command-time values, not a borrowed mutable camera. Angles use
    // protocol degrees (positive pitch down). Missing observations stay absent.
    std::optional<std::array<float,3>> view_angles_degrees, punch_angles_degrees, player_velocity;
    // Exact immutable command pose, distinct from the smoothed camera eye.
    // Absent if neither the matching prediction entry nor canonical pose exists.
    std::optional<std::array<float,3>> player_origin;
    std::optional<bool> player_ducked;
    friend bool operator==(const ShotContext& a,const ShotContext& b) noexcept {
      return a.eye.x==b.eye.x && a.eye.y==b.eye.y && a.eye.z==b.eye.z &&
          a.direction.x==b.direction.x && a.direction.y==b.direction.y &&
          a.direction.z==b.direction.z && a.right.x==b.right.x && a.right.y==b.right.y &&
          a.right.z==b.right.z && a.up.x==b.up.x && a.up.y==b.up.y && a.up.z==b.up.z &&
          a.view_angles_degrees==b.view_angles_degrees &&
          a.punch_angles_degrees==b.punch_angles_degrees && a.player_velocity==b.player_velocity &&
          a.player_origin==b.player_origin && a.player_ducked==b.player_ducked;
    }
  };
  std::optional<ShotContext> shot_context;
  friend bool operator==(const LocalWeaponSubmittedCommand&,
                         const LocalWeaponSubmittedCommand&) = default;
};
// Neutral command-local ordering; default callers retain the pre-step delta.
// Weapon post-think effects must not advance the firing command's position.
enum class CommandMovementEffectPhase : std::uint8_t {
  before_movement, after_movement
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
  // Bounded effect occurrence within one causal action (zero is the action).
  // Separate pellets/casings retain the parent identity without aliasing pools.
  std::uint32_t sub_identity{};
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
  std::size_t secondary_launch_starts{}, secondary_launch_confirmed{};
  std::size_t secondary_fire_starts{}, secondary_fire_confirmed{};
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
  // Horizontal degrees (actual viewport unless a reference aspect is set);
  // absence preserves the host projection. No concrete weapon/message policy.
  std::optional<double> horizontal_field_of_view_degrees;
  // Optional reference aspect for that horizontal angle. When supplied, keep
  // its vertical coverage as the viewport changes (Hor+); absence retains the
  // actual-viewport horizontal policy used by alternate modules.
  std::optional<double> field_of_view_reference_aspect_ratio;
  enum class ProjectionStatus : std::uint8_t { unavailable, server_confirmed, reset, invalid };
  ProjectionStatus projection_status{ProjectionStatus::unavailable};
  CameraIntentStatus status{CameraIntentStatus::ready};
  bool allow_predicted_translation{true};
  // Presentation only. Zero is latest-authoritative; positive is an absolute
  // time bounded transition after a fresh sample, never physics/extrapolation.
  double authoritative_translation_blend_seconds{};
  std::optional<std::array<float, 3U>> server_punch_angle;
  double pitch_offset_degrees{}, local_pitch_offset_degrees{};
  double yaw_offset_degrees{}, roll_degrees{};
  // Dimensionless physical mouse-look multiplier, selected with projection.
  // One preserves base user sensitivity; the input mechanism validates (0,4].
  // No weapon, FOV formula or settings ownership crosses this neutral seam.
  double mouse_sensitivity_scale{1.0};
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
  // Presentation-space translation shared by geometry and evaluated sockets.
  // Source model units, +X forward/+Y left/+Z up; no canonical state mutation.
  std::array<float, 3U> camera_local_origin{};
  friend bool operator==(const ViewmodelIntent&, const ViewmodelIntent&) = default;
};
struct HudRectangle final {
  float x{}, y{}, width{}, height{};
  std::array<float, 4U> color{};
  std::uint32_t draw_order{};
  friend bool operator==(const HudRectangle&, const HudRectangle&) = default;
};
struct HudText final {
  std::string text;
  float x{}, y{}, pixel_size{}, advance{}, line_height{};
  std::array<float, 4U> color{};
  std::uint32_t draw_order{};
  friend bool operator==(const HudText&, const HudText&) = default;
};
struct HudSourceRectangle final {
  std::uint32_t x{}, y{}, width{}, height{};
  friend bool operator==(const HudSourceRectangle&, const HudSourceRectangle&) = default;
};
enum class HudSpriteBlend : std::uint8_t { opaque, straight_alpha, additive, alpha_test };
// Screen-space image intent. An immutable approved resource is bound by the
// application; no file name, GPU object or game meaning reaches the renderer.
struct HudSprite final {
  std::uint64_t resource_id{}, resource_revision{};
  std::uint32_t frame{};
  HudSourceRectangle source;
  float x{}, y{}, width{}, height{};
  std::array<float, 4U> color{};
  HudSpriteBlend blend{HudSpriteBlend::additive};
  std::optional<HudRectangle> clip;
  std::uint32_t draw_order{};
  friend bool operator==(const HudSprite&, const HudSprite&) = default;
};
inline constexpr std::size_t kMaximumHudRectangles = 64U;
inline constexpr std::size_t kMaximumHudTexts = 224U;
inline constexpr std::size_t kMaximumHudTextBytes = 160U;
inline constexpr std::size_t kMaximumHudTotalTextBytes = 16'384U;
inline constexpr std::size_t kMaximumHudGlyphs = 8'192U;
inline constexpr std::size_t kMaximumHudSprites = 256U;
inline constexpr float kMaximumHudCoordinate = 32'768.0F;
inline constexpr double kMaximumHudSpritePixelArea = 64.0 * 1024.0 * 1024.0;
// Includes worst-case 32 player + 32 team rows, four notices and local HUD.
// Coordinates use top-left pixel space. Renderer supplies only glyph geometry.
struct HudDrawCommands final {
  std::vector<HudRectangle> rectangles;
  std::vector<HudText> texts;
  std::vector<HudSprite> sprites;
  friend bool operator==(const HudDrawCommands&, const HudDrawCommands&) = default;
};
[[nodiscard]] inline bool valid_hud_draw_commands(const HudDrawCommands& draw) noexcept {
  if (draw.rectangles.size() > kMaximumHudRectangles || draw.texts.size() > kMaximumHudTexts ||
      draw.sprites.size() > kMaximumHudSprites) return false;
  std::size_t bytes = 0U, glyphs = 0U;
  const auto color_valid = [](const auto& color) {
    for (const auto value : color) if (!std::isfinite(value) || value < 0.0F || value > 1.0F) return false;
    return true;
  };
  for (const auto& rect : draw.rectangles) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
        !std::isfinite(rect.height) || rect.x < 0.0F || rect.y < 0.0F || rect.width < 0.0F ||
        rect.height < 0.0F || !color_valid(rect.color)) return false;
  }
  for (const auto& text : draw.texts) {
    if (text.text.size() > kMaximumHudTextBytes || !std::isfinite(text.x) || !std::isfinite(text.y) ||
        !std::isfinite(text.pixel_size) || !std::isfinite(text.advance) || !std::isfinite(text.line_height) ||
        text.x < 0.0F || text.y < 0.0F || text.pixel_size <= 0.0F || text.advance <= 0.0F ||
        text.line_height <= 0.0F || !color_valid(text.color)) return false;
    bytes += text.text.size();
    for (const unsigned char c : text.text) {
      if (c == '\n') continue;
      if (c < 32U || c > 126U) return false;
      ++glyphs;
    }
  }
  double sprite_area = 0.0;
  const auto screen_rect_valid = [](const float x, const float y,
                                    const float width, const float height) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
        std::abs(x) <= kMaximumHudCoordinate && std::abs(y) <= kMaximumHudCoordinate &&
        width > 0.0F && height > 0.0F && width <= kMaximumHudCoordinate && height <= kMaximumHudCoordinate;
  };
  for (const auto& sprite : draw.sprites) {
    if (!sprite.resource_id || !sprite.resource_revision || !sprite.source.width || !sprite.source.height ||
        !screen_rect_valid(sprite.x,sprite.y,sprite.width,sprite.height) || !color_valid(sprite.color) ||
        (sprite.blend != HudSpriteBlend::opaque && sprite.blend != HudSpriteBlend::straight_alpha &&
         sprite.blend != HudSpriteBlend::additive && sprite.blend != HudSpriteBlend::alpha_test)) return false;
    if (sprite.clip && !screen_rect_valid(sprite.clip->x,sprite.clip->y,
        sprite.clip->width,sprite.clip->height)) return false;
    sprite_area += static_cast<double>(sprite.width) * sprite.height;
    if (sprite_area > kMaximumHudSpritePixelArea) return false;
  }
  return bytes <= kMaximumHudTotalTextBytes && glyphs <= kMaximumHudGlyphs;
}
struct HudState final {
  HudDrawCommands draw;
  bool draw_rejected{};
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
  // Bounded presentation diagnostics. They distinguish an intentional absent
  // role from unresolved/invalid resources; submitted is not pixel acceptance.
  std::string crosshair_status, crosshair_role;
  std::uint32_t hud_resource_tier{};
  std::uint32_t hud_canvas_width{}, hud_canvas_height{};
  float hud_automatic_scale{1.0F}, hud_user_scale{1.0F}, hud_effective_scale{1.0F};
  float killfeed_font_pixel_size{}, killfeed_icon_scale{}, killfeed_maximum_row_width{};
  std::uint64_t crosshair_resource_id{};
  bool weapon_menu_open{};
  std::optional<std::uint8_t> weapon_menu_candidate, weapon_selection_pending;
  std::optional<std::uint8_t> weapon_selection_request;
  std::string weapon_selection_status;
  std::size_t pickup_icons_submitted{}, pickup_icons_unavailable{};
};
} // namespace hlclient::game_api

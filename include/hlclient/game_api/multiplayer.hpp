#pragma once
#include <array>
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/game_api/player_appearance.hpp>
#include <optional>
#include <string>

namespace hlclient::game_api {
inline constexpr std::size_t kMaximumHudPlayers = 32U;
inline constexpr std::size_t kMaximumPlayerDisplayBytes = 31U;
inline constexpr std::size_t kMaximumDeathNotices = 4U;
// Owning display-only projection; only bounded name and typed appearance cross
// this seam, never raw userinfo or authentication identifiers. Replacement is
// established in the protocol owner, not from names.
struct PlayerDisplayUpdate final {
  std::uint32_t slot{};
  bool present{}, new_occupant{};
  std::string name;
  PlayerAppearanceFields appearance;
};
// Server-observed connection statistics, already mapped to one-based slots.
// Presence and occupant identity remain owned by the player directory.
struct PlayerNetworkUpdate final {
  std::uint32_t slot{};
  std::uint16_t ping_ms{};
  std::uint8_t packet_loss{};
};
struct MultiplayerPlayerHudEntry final {
  bool present{}, has_score{};
  std::string name, team;
  std::int16_t frags{}, deaths{}, player_class{}, team_number{};
  std::uint64_t name_revision{}, score_revision{};
  // Explicit committed directory evidence, distinct from an unobserved slot.
  bool identity_observed{};
  std::uint64_t occupant_epoch{};
  PlayerAppearanceFields appearance;
  std::uint64_t appearance_revision{};
  std::optional<std::uint16_t> ping_ms;
  std::optional<std::uint8_t> packet_loss;
};
struct MultiplayerTeamHudEntry final {
  std::string name;
  std::int16_t frags{}, deaths{};
};
enum class DeathNoticeKind : std::uint8_t {
  normal,
  world,
  suicide,
  non_player,
  teammate
};
struct DeathNoticeState final {
  client::RuntimeObservationSource source;
  std::uint32_t killer{}, victim{};
  std::string killer_name, victim_name, weapon;
  DeathNoticeKind kind{};
  std::optional<double> created_seconds;
  // Display-only score-directory snapshots at this notice's atomic commit.
  // A reused player slot must not recolor the retained original names.
  std::optional<std::int16_t> killer_team, victim_team;
};
// Transaction-owned semantic state. Generic hosts only move/validate bounds;
// concrete modules alone interpret scores, teams and notice classifications.
struct MultiplayerHudState final {
  std::array<MultiplayerPlayerHudEntry, kMaximumHudPlayers> players;
  std::array<MultiplayerTeamHudEntry, kMaximumHudPlayers> teams;
  std::array<std::optional<DeathNoticeState>, kMaximumDeathNotices> notices;
  std::optional<client::RuntimeObservationSource> high_water;
  std::uint32_t maximum_clients{32U};
  std::optional<std::uint32_t> local_slot;
  bool teamplay{};
};
// Immutable presentation pose for directional screen feedback. Body origin
// and camera basis are observed locally; no entity/physics state is mutated.
struct HudViewContext final {
  std::array<double,3> player_origin{}, forward{}, right{};
};
struct HudPresentationContext final {
  std::uint32_t viewport_width{1280U}, viewport_height{720U};
  bool scoreboard_held{};
  bool hud_draw{true}, crosshair{true}, fastswitch{}, diagnostic_overlay{};
  // Physical framebuffer pixels, never logical window/DPI coordinates. The
  // module applies this optional user multiplier once to its own UI metrics.
  bool hud_scale_automatic{true};
  float hud_scale{1.0F};
  std::optional<HudViewContext> view;
};
} // namespace hlclient::game_api

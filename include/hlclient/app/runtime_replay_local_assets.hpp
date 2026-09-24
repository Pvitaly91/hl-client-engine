#pragma once

#include <hlclient/app/runtime_replay_visual_projection.hpp>
#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/runtime_replay_capture.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <cstdint>
#include <map>

namespace hlclient::app {

enum class ReplayLocalCameraPolicy : std::uint8_t {
  offline_spectator,
  external_live_receiving_client,
};

enum class ReplayLocalVisualStatus {
  ready_studio,
  ready_sprite,
  absent_model,
  unknown_model_slot,
  inline_brush_unsupported,
  missing_asset,
  unsupported_asset,
  unsupported_schema,
  incomplete_transform,
  unsupported_render_mode,
  hidden_by_effects,
  unsupported_pose,
  unsupported_material,
  unsafe_asset,
  ambiguous_asset,
  dependency_missing,
  import_failed,
};
[[nodiscard]] std::string_view
to_string(ReplayLocalVisualStatus status) noexcept;

struct ReplayLocalVisualSummary {
  std::string map;
  std::string precache_completeness;
  std::size_t captured_resources{};
  std::size_t imported_studio{};
  std::size_t imported_sprites{};
  std::size_t size_matches{};
  std::size_t size_unavailable{};
  std::size_t decoded_entities{};
  std::size_t resolved_instances{};
  std::size_t rendered_instances{};
  std::size_t unsupported_instances{};
  std::size_t projected_frames{};
  std::size_t unchanged_records{};
  std::map<ReplayLocalVisualStatus, std::size_t> coverage;
  std::map<std::uint32_t, std::string> model_names;
  std::map<std::uint32_t, std::size_t> rendered_model_slots;
};

class RuntimeReplayLocalAssets;
struct ReplayLocalAssetsCreateResult {
  std::unique_ptr<RuntimeReplayLocalAssets> projection;
  std::optional<RuntimeReplayVisualProjectionError> error;
};

// Offline composition only. All disk reads pass through the existing rooted
// environment and approved source capabilities; renderer inputs stay neutral.
class RuntimeReplayLocalAssets final {
public:
  [[nodiscard]] static ReplayLocalAssetsCreateResult
  create(const goldsrc::RuntimeReplayCaptureState &capture,
         const std::filesystem::path &basedir, std::string_view game);
  [[nodiscard]] static ReplayLocalAssetsCreateResult
  create(const goldsrc::ResourceListState &resources,
         const goldsrc::ServerInfoState &server_info,
         const std::filesystem::path &basedir, std::string_view game,
         ReplayLocalCameraPolicy camera_policy =
             ReplayLocalCameraPolicy::offline_spectator);
  [[nodiscard]] RuntimeReplayVisualProjectionResult
  reset_generation(const client::RuntimeClientObservationState &,
                   client::ClientWorldState &);
  [[nodiscard]] RuntimeReplayVisualProjectionResult
  project_entities(const client::RuntimeClientObservationState &,
                   client::ClientWorldState &);
  void acknowledge_unchanged() noexcept { ++summary_.unchanged_records; }
  [[nodiscard]] const ReplayLocalVisualSummary &summary() const noexcept {
    return summary_;
  }
  // Borrow the already imported map collision package for live prediction.
  // Ownership remains with this asset projection; no second BSP parse.
  [[nodiscard]] const std::shared_ptr<const collision::CollisionWorldPackage> &
  collision_world_package() const noexcept { return camera_collision_; }

private:
  struct Binding {
    ReplayLocalVisualStatus status{ReplayLocalVisualStatus::unsupported_asset};
    std::optional<std::size_t> library_index;
    std::optional<std::size_t> render_index;
  };
  ReplayLocalVisualSummary summary_;
  std::map<std::uint32_t, Binding> bindings_;
  std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState> library_;
  std::shared_ptr<const entity_render::EntitySceneRenderPackage> package_;
  std::shared_ptr<const world_render::WorldRenderPackage> world_;
  std::shared_ptr<const collision::CollisionWorldPackage> camera_collision_;
  ReplayLocalCameraPolicy camera_policy_{
      ReplayLocalCameraPolicy::offline_spectator};
  std::uint64_t generation_{};
  std::uint64_t frame_revision_{};
  bool camera_selected_{};
};
} // namespace hlclient::app

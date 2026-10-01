#pragma once
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>

#include <hlclient/app/runtime_replay_visual_projection.hpp>
#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/runtime_replay_capture.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <hlclient/world_render/world_render_types.hpp>
#include <cstdint>
#include <array>
#include <map>

namespace hlclient::app {

// Temporal presentation of existing D3 instances only; never inserts a hidden
// or removed entity, changes materials, or writes back to ClientWorldState.
[[nodiscard]] std::shared_ptr<const world_scene_render::RuntimeBrushRenderFrame>
present_runtime_brushes(const world_scene_render::RuntimeBrushRenderFrame& committed,
    const goldsrc::collision::BrushCollisionScene& sampled, std::uint64_t generation);

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
  ready_brush,
  invalid_brush_reference,
  missing_brush_geometry,
  unsupported_brush_transform,
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

enum class ReplayShellFrameStatus : std::uint8_t {
  not_attempted, invalid_input, binding_unavailable, pose_rejected,
  material_rejected, no_valid_instances, frame_rejected, submitted,
};
enum class LocalImpactDecalStatus : std::uint8_t {
  not_requested, ready, absent, rejected
};
[[nodiscard]] constexpr std::string_view to_string(LocalImpactDecalStatus status) noexcept {
  switch (status) {
  case LocalImpactDecalStatus::not_requested: return "not_requested";
  case LocalImpactDecalStatus::ready: return "ready";
  case LocalImpactDecalStatus::absent: return "absent";
  case LocalImpactDecalStatus::rejected: return "rejected";
  }
  return "rejected";
}
[[nodiscard]] std::string_view to_string(ReplayShellFrameStatus) noexcept;
[[nodiscard]] std::string_view
to_string(ReplayLocalVisualStatus status) noexcept;

struct ReplayLocalVisualSummary {
  struct RemotePlayer {
    std::uint32_t entity{}, model_slot{};
    game_api::RemotePlayerPresentationStatus status{};
    game_api::RemotePlayerPresentationIntent intent;
    std::array<double,3> origin{}, source_angles{};
    std::optional<std::array<float,3>> static_light;
    bool pose_submitted{}, visible{};
  };
  std::array<RemotePlayer,game_api::kMaximumRemotePlayers> remote_players{};
  std::size_t remote_player_count{};
  double remote_previous_seconds{},remote_current_seconds{},remote_sample_seconds{},remote_alpha{};
  struct Rejection {
    std::uint32_t entity_number{}, model_slot{};
    std::optional<std::uint32_t> submodel;
    ReplayLocalVisualStatus reason{};
    std::uint64_t revision{};
  };
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
  std::size_t prepared_brush_models{};
  std::size_t missing_texture_placeholder_bindings{};
  std::size_t brush_candidates{};
  std::size_t resolved_brushes{};
  std::size_t submitted_brushes{};
  std::size_t hidden_brushes{};
  std::size_t unsupported_brush_materials{};
  std::size_t brush_transform_changes{};
  std::optional<std::uint32_t> last_changed_brush_entity, last_changed_brush_model;
  std::optional<Rejection> first_brush_rejection;
  std::map<ReplayLocalVisualStatus, std::size_t> coverage;
  std::map<std::uint32_t, std::string> model_names;
  std::map<std::uint32_t, std::size_t> rendered_model_slots;
};

class RuntimeReplayLocalAssets;
struct ReplayLocalAssetsCreateResult {
  std::unique_ptr<RuntimeReplayLocalAssets> projection;
  std::optional<RuntimeReplayVisualProjectionError> error;
};

// Normal/offline composition. All disk reads pass through the existing rooted
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
  // Same materializer, presentation-only sample. The sole application clock
  // explicitly anchors to committed svc_time; sampling never changes RX state.
  [[nodiscard]] RuntimeReplayVisualProjectionResult present_entities(
      game_api::GameClientHost&, double application_seconds,
      std::optional<std::uint32_t> receiving_entity, client::ClientWorldState&,
      renderer::RenderExtent);
  void player_slot_boundary(std::uint32_t entity) noexcept;
  // Lost committed slot events require a conservative visual continuity reset,
  // not a network/map reset. Fresh entity records can materialize new occupants.
  void invalidate_player_continuity() noexcept;
  void acknowledge_unchanged() noexcept { ++summary_.unchanged_records; }
  [[nodiscard]] const ReplayLocalVisualSummary &summary() const noexcept {
    return summary_;
  }
  // Borrow the already imported map collision package for live prediction.
  // Ownership remains with this asset projection; no second BSP parse.
  [[nodiscard]] const std::shared_ptr<const collision::CollisionWorldPackage> &
  collision_world_package() const noexcept { return camera_collision_; }
  [[nodiscard]] const std::shared_ptr<const world_scene_render::WorldSceneRenderPackage>&
  surface_scene() const noexcept { return world_scene_; }
  [[nodiscard]] std::string_view movement_materials() const noexcept { return movement_materials_; }
  const goldsrc::PreparedSoundResources& sound_resources() const noexcept {return sound_resources_;}
  // Exact-root, verified-open derived resource. One preparation per map; no
  // network path or arbitrary OS filename reaches this API.
  // Two bounded independent game-selected profiles share the same exact-root
  // importer; slot 0 is the pre-existing Glock path, slot 1 is crowbar.
  LocalImpactDecalStatus prepare_impact_decal(const game_api::LocalImpactAssetProfile&,
      std::size_t slot=0U) noexcept;
  LocalImpactDecalStatus impact_decal_status(std::size_t slot=0U) const noexcept {
    return slot<impact_decal_statuses_.size() ? impact_decal_statuses_[slot] : LocalImpactDecalStatus::rejected;
  }
  const std::shared_ptr<const assets::WorldTextureAsset>& impact_decal_texture(std::size_t slot=0U) const noexcept {
    return impact_decal_textures_[slot<impact_decal_textures_.size()?slot:0U];
  }
  void sound_sources(const client::RuntimeClientObservationState&,
      const goldsrc::collision::BrushCollisionScene*, std::vector<goldsrc::SoundSource>&) const;
  // Mechanism only: query imported metadata and execute a game-selected pose.
  // No gameplay state, weapon IDs or model-name policy enters this materializer.
  [[nodiscard]] std::optional<renderer::RenderDynamicEntities>
  materialize_viewmodel(const game_api::ViewmodelIntent&);
  [[nodiscard]] std::optional<assets::AssetVector3> viewmodel_attachment_zero() const noexcept {
    return viewmodel_attachment_zero_;
  }
  // Evaluate a marker attachment through the same CPU Studio pose evaluator
  // used for the viewmodel. This is camera-local, never a world-space origin.
  [[nodiscard]] std::optional<assets::AssetVector3> viewmodel_attachment_at_frame(
      const game_api::ViewmodelIntent&, std::uint32_t attachment_index,
      double frame_coordinate) const;
  [[nodiscard]] bool has_shell_model() const noexcept { return shell_model_index_.has_value(); }
  [[nodiscard]] ReplayLocalVisualStatus shell_resource_status() const noexcept {
    return shell_resource_status_;
  }
  [[nodiscard]] ReplayShellFrameStatus last_shell_frame_status() const noexcept {
    return last_shell_frame_status_;
  }
  [[nodiscard]] std::optional<renderer::RenderDynamicEntities>
  materialize_shells(std::span<const renderer::TransientShellState>, double now);
  [[nodiscard]] std::optional<game_api::LocalWeaponModelMetadata>
  presentation_model(std::uint64_t generation, std::uint32_t model_index);
  // Immutable, retained metadata from an already approved/imported Studio slot.
  // No asset I/O or mutable engine state escapes this read view.
  [[nodiscard]] std::shared_ptr<const assets::SkeletalModelAssetData>
  studio_model_data(std::uint32_t model_index) const noexcept;
  [[nodiscard]] ReplayLocalVisualStatus first_person_status() const noexcept {
    return first_person_status_;
  }

private:
  struct Binding {
    ReplayLocalVisualStatus status{ReplayLocalVisualStatus::unsupported_asset};
    std::optional<std::size_t> library_index;
    std::optional<std::size_t> render_index;
    std::optional<std::uint32_t> brush_model;
    assets::AssetVector3 brush_source_origin{};
  };
  ReplayLocalVisualSummary summary_;
  goldsrc::PreparedSoundResources sound_resources_;
  std::array<LocalImpactDecalStatus,2U> impact_decal_statuses_{
      LocalImpactDecalStatus::not_requested,LocalImpactDecalStatus::not_requested};
  std::array<std::shared_ptr<const assets::WorldTextureAsset>,2U> impact_decal_textures_{};
  std::string movement_materials_;
  std::map<std::uint32_t, Binding> bindings_;
  std::map<std::uint32_t, game_api::LocalWeaponModelMetadata> presentation_models_;
  std::uint32_t maximum_clients_{};
  std::shared_ptr<const client::RuntimeClientObservationState> previous_entities_, current_entities_;
  struct PlayerLifetime { std::uint64_t identity{}; std::optional<std::uint32_t> model; std::uint64_t continuity_floor{}; };
  std::map<std::uint32_t, PlayerLifetime> player_lifetimes_;
  std::uint64_t next_player_lifetime_{}, clock_record_{};
  double clock_anchor_seconds_{}, sampled_server_seconds_{};
  double sampled_previous_seconds_{}, sampled_current_seconds_{}, sampled_alpha_{};
  game_api::GameClientHost* sampled_game_{}; // synchronous borrow only
  std::optional<std::uint32_t> sampled_receiving_entity_;
  std::optional<renderer::RenderExtent> sampled_extent_;
  std::map<std::uint32_t, std::pair<assets::AssetVector3, std::optional<std::array<float,3>>>> light_cache_;
  std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState> library_;
  std::shared_ptr<const entity_render::EntitySceneRenderPackage> package_;
  std::shared_ptr<const world_render::WorldRenderPackage> world_;
  std::shared_ptr<const world_scene_render::WorldSceneRenderPackage> world_scene_;
  std::shared_ptr<const collision::CollisionWorldPackage> camera_collision_;
  ReplayLocalCameraPolicy camera_policy_{
      ReplayLocalCameraPolicy::offline_spectator};
  std::uint64_t generation_{};
  std::uint64_t frame_revision_{};
  bool camera_selected_{};
  std::uint64_t viewmodel_frame_revision_{};
  std::uint64_t shell_frame_revision_{};
  std::optional<std::uint32_t> shell_model_index_;
  ReplayLocalVisualStatus shell_resource_status_{ReplayLocalVisualStatus::absent_model};
  ReplayShellFrameStatus last_shell_frame_status_{ReplayShellFrameStatus::not_attempted};
  std::optional<assets::AssetVector3> viewmodel_attachment_zero_;

  ReplayLocalVisualStatus first_person_status_{ReplayLocalVisualStatus::absent_model};
};
} // namespace hlclient::app

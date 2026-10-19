#pragma once
#include <hlclient/app/entity_follow_presentation.hpp>
#include <hlclient/app/loading_presentation.hpp>
#include <hlclient/app/remote_movement_diagnostics.hpp>
#include <hlclient/app/world_impact_presentation.hpp>
#include <hlclient/game_api/presentation.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/sound_assets.hpp>
#include <hlclient/goldsrc/sky_assets.hpp>
#include <hlclient/goldsrc/player_model_assets.hpp>
#include <hlclient/goldsrc/decal_assets.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_entity_animation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <hlclient/app/runtime_replay_visual_projection.hpp>
#include <hlclient/collision/collision_world_package.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/runtime_replay_capture.hpp>
#include <hlclient/goldsrc/collision/goldsrc_brush_collision_scene.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/renderer/transient_visuals.hpp>
#include <hlclient/app/temporary_effect_presentation.hpp>
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

// Resource identity is independent of the camera or server address. Recorded
// evidence retains its exact-size guard. Live HL uses map-lump CRC identity;
// resource-list download sizes are only diagnostic metadata.
enum class ReplayLocalResourcePolicy : std::uint8_t {
  recorded_exact_size,
  live_standard_hl,
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
struct ServerDecalMaterializationResult {
  goldsrc::DecalAssetStatus asset_status{goldsrc::DecalAssetStatus::pending};
  ServerDecalResult placement;
};
enum class ReplayAttachmentStatus : std::uint8_t {
  absent, unknown_slot, resource_unavailable, merge_rejected, pose_rejected,
  material_rejected, submitted, visible, culled
};
[[nodiscard]] constexpr std::string_view to_string(ReplayAttachmentStatus status) noexcept {
  switch(status) {
  case ReplayAttachmentStatus::absent:return "absent";
  case ReplayAttachmentStatus::unknown_slot:return "unknown_slot";
  case ReplayAttachmentStatus::resource_unavailable:return "resource_unavailable";
  case ReplayAttachmentStatus::merge_rejected:return "merge_rejected";
  case ReplayAttachmentStatus::pose_rejected:return "pose_rejected";
  case ReplayAttachmentStatus::material_rejected:return "material_rejected";
  case ReplayAttachmentStatus::submitted:return "submitted";
  case ReplayAttachmentStatus::visible:return "visible";
  case ReplayAttachmentStatus::culled:return "culled";
  }
  return "invalid";
}
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
[[nodiscard]] constexpr std::string_view remote_snapshot_mode_name(
    goldsrc::EntitySnapshotPairSelectionStatus status) noexcept {
  switch(status) {
  case goldsrc::EntitySnapshotPairSelectionStatus::bracketed:return "bracketed";
  case goldsrc::EntitySnapshotPairSelectionStatus::exact_previous:return "exact_previous";
  case goldsrc::EntitySnapshotPairSelectionStatus::exact_current:return "exact_current";
  case goldsrc::EntitySnapshotPairSelectionStatus::held_oldest:return "held_oldest";
  case goldsrc::EntitySnapshotPairSelectionStatus::held_newest:return "held_newest";
  case goldsrc::EntitySnapshotPairSelectionStatus::held_only:return "held_only";
  }
  return "unavailable";
}

struct ReplayLocalVisualSummary {
  struct ResourceIssue {
    std::string virtual_name;
    std::string reason;
    // Type 255 is a local WAD dependency; its index is the inert worldspawn
    // declaration ordinal, not a server precache slot.
    std::uint16_t resource_index{};
    std::uint8_t resource_type{};
  };
  // Bounded approved virtual-name diagnostics only; no native roots/locators.
  std::vector<ResourceIssue> resource_issues;
  std::size_t resource_issues_omitted{};
  struct RemotePlayer {
    std::uint32_t entity{}, model_slot{};
    game_api::RemotePlayerPresentationStatus status{};
    game_api::RemotePlayerPresentationIntent intent;
    std::array<double,3> origin{}, source_angles{};
    std::optional<std::array<float,3>> static_light;
    bool pose_submitted{}, visible{};
    bool server_owned_proxy{};
    std::optional<std::uint32_t> source_player;
    game_api::WorldEntityPresentationReason classification{};
    std::uint64_t network_generation{},map_generation{},entity_lifetime{};
    std::optional<std::uint32_t> render_effect,render_amount,weapon_model_index,gait_sequence;
    std::optional<std::uint32_t> support_entity,support_model;
    std::optional<double> support_offset_z;
    std::uint32_t effects{};
    bool no_interpolation{};
    ReplayAttachmentStatus attachment_status{ReplayAttachmentStatus::absent};
    std::size_t attachment_matched_bones{};
    std::size_t attachment_bone_count{};
    game_api::PlayerAppearanceSelection appearance;
    std::string resolved_visual_model;
    std::uint64_t visual_model_resource_id{};
    bool palette_remapped{};
  };
  std::array<RemotePlayer,game_api::kMaximumRemotePlayers> remote_players{};
  std::size_t remote_player_count{};
  double remote_previous_seconds{},remote_current_seconds{},remote_sample_seconds{},remote_alpha{};
  std::optional<goldsrc::EntitySnapshotPairSelectionStatus> remote_snapshot_mode;
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
  std::size_t size_mismatches{};
  bool world_crc_verified{}; // Actual multiplayer BSP lump CRC matched.
  std::size_t decoded_entities{};
  std::size_t resolved_instances{};
  std::size_t rendered_instances{};
  std::size_t unsupported_instances{};
  std::size_t projected_frames{};
  std::size_t unchanged_records{};
  // Last materialization only; ordinary poses share exact immutable inputs
  // within that call, never across frames, generations, or player composition.
  std::size_t ordinary_pose_evaluations{}, ordinary_pose_reuses{};
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
  // Failed preparation must retain the same bounded approved diagnostics even
  // though no partially prepared projection is published.
  std::vector<ReplayLocalVisualSummary::ResourceIssue> resource_issues{};
  std::size_t resource_issues_omitted{};
};

// Normal/offline composition. All disk reads pass through the existing rooted
// environment and approved source capabilities; renderer inputs stay neutral.
class RuntimeReplayLocalAssets final : public game_api::IStaticEffectTrace {
public:
  // Borrowed session-thread observer only. A zero map generation is unknown;
  // the owning committed observation supplies the network generation.
  void set_diagnostic_sink(core::MultiplayerDiagnosticSink*,
      std::uint64_t map_generation = 0U) noexcept;
  [[nodiscard]] std::optional<double> effective_remote_interpolation_delay_seconds() const noexcept {
    return player_presentation_.effective_delay_seconds();
  }
  game_api::StaticEffectTrace trace(assets::AssetVector3,assets::AssetVector3) const noexcept override;
  game_api::PlacementTraceResult placement_trace(const game_api::PlacementTraceRequest&) const noexcept override;
  void placement_observation(std::shared_ptr<const client::RuntimeClientObservationState> state) noexcept {
    placement_entities_=std::move(state);
  }
  [[nodiscard]] static ReplayLocalAssetsCreateResult
  create(const goldsrc::RuntimeReplayCaptureState &capture,
         const std::filesystem::path &basedir, std::string_view game,
         std::shared_ptr<LoadingWork> loading_work = {});
  [[nodiscard]] static ReplayLocalAssetsCreateResult
  create(const goldsrc::ResourceListState &resources,
         const goldsrc::ServerInfoState &server_info,
         const std::filesystem::path &basedir, std::string_view game,
         ReplayLocalCameraPolicy camera_policy =
             ReplayLocalCameraPolicy::offline_spectator,
         std::string_view effect_palette_dependency = {},
         std::span<const std::string> effect_sprite_dependencies = {},
         std::string_view sky_name = {},
         std::shared_ptr<LoadingWork> loading_work = {},
         ReplayLocalResourcePolicy resource_policy =
             ReplayLocalResourcePolicy::recorded_exact_size);
  [[nodiscard]] RuntimeReplayVisualProjectionResult
  reset_generation(const client::RuntimeClientObservationState &,
                   client::ClientWorldState &);
  [[nodiscard]] RuntimeReplayVisualProjectionResult
  project_entities(const client::RuntimeClientObservationState &,
                   client::ClientWorldState &);
  // Retain one authoritative entity observation for the normal presentation
  // clock without constructing or publishing a discrete render frame.  This
  // owns the same bounded histories, lifetimes, follows, decals and effect
  // sources as project_entities; a later present_entities call materializes
  // the sole visible sampled frame.  No ClientWorldState is mutated here.
  [[nodiscard]] RuntimeReplayVisualProjectionResult
  observe_entities(const client::RuntimeClientObservationState &);
  // Same materializer, presentation-only sample. The sole application clock
  // explicitly anchors to committed svc_time; sampling never changes RX state.
  [[nodiscard]] RuntimeReplayVisualProjectionResult present_entities(
      game_api::GameClientHost&, double application_seconds,
      std::optional<std::uint32_t> receiving_entity, client::ClientWorldState&,
      renderer::RenderExtent, bool capture_remote_diagnostics = false,
      std::shared_ptr<const goldsrc::collision::BrushCollisionScene> rendered_brush_scene = {});
  // Unavailable is not an empty actor list/removal. These observations belong
  // only to the successfully published normal presentation.
  [[nodiscard]] std::optional<std::span<const RemoteMovementObservation>>
  remote_movement_observations() const noexcept {
    if (!remote_movement_valid_) return {};
    return std::span<const RemoteMovementObservation>{remote_movement_};
  }
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
  // Effect collision is available independently of the selected movement
  // predictor. Reuses approved model hulls and caches one committed entity
  // publication; no per-ray rebuild, file I/O or inferred gameplay solidity.
  [[nodiscard]] std::shared_ptr<const goldsrc::collision::BrushCollisionScene>
  impact_brush_scene() const noexcept;
  [[nodiscard]] std::string_view movement_materials() const noexcept { return movement_materials_; }
  [[nodiscard]] const game_api::EffectPaletteMetadata& effect_palette() const noexcept {return effect_palette_;}
  const goldsrc::PreparedSoundResources& sound_resources() const noexcept {return sound_resources_;}
  [[nodiscard]] const std::shared_ptr<const assets::SkyboxAsset>& skybox() const noexcept {return sky_assets_.skybox;}
  [[nodiscard]] goldsrc::SkyAssetStatus skybox_status() const noexcept {return sky_assets_.status;}
  // Exact-root, verified-open derived resource. One preparation per map; no
  // network path or arbitrary OS filename reaches this API.
  // Two bounded independent game-selected profiles share the same exact-root
  // importer; slot 0 is the pre-existing Glock path, slot 1 is crowbar.
  LocalImpactDecalStatus prepare_impact_decal(const game_api::LocalImpactAssetProfile&,
      std::size_t slot=0U) noexcept;
  // Compiled dependencies prepared once at startup/readiness. A server slot
  // names a selected recipe, but never grants file-opening authority.
  const goldsrc::PreparedDecalAssets& prepare_surface_decals(
      std::span<const game_api::DecalAssetRequest>) noexcept;
  [[nodiscard]] ServerDecalMaterializationResult materialize_server_decal(
      const game_api::TemporaryEffectIntent&,WorldImpactPresentation&,
      std::uint64_t resource_generation) const noexcept;
  [[nodiscard]] std::span<const WorldImpactPresentation::ServerDecalTargetLifetime>
  server_decal_lifetimes() const noexcept {return server_decal_lifetimes_;}
  [[nodiscard]] WorldImpactPresentation::BrushImpactContext brush_impact_context(
      std::size_t slot=0U) const noexcept {
    if(slot>=impact_decal_textures_.size())return {};
    return {world_scene_ ? &world_scene_->brush_library() : nullptr,
        server_decal_lifetimes_,impact_decal_textures_[slot],impact_decal_modes_[slot]};
  }
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
  materialize_viewmodel(const game_api::ViewmodelIntent&,
      std::optional<assets::AssetVector3> world_light_origin = {});
  [[nodiscard]] std::optional<assets::AssetVector3> viewmodel_attachment_zero() const noexcept {
    return viewmodel_attachment_zero_;
  }
  // Evaluate a marker attachment through the same CPU Studio pose evaluator
  // used for the viewmodel. This is camera-local, never a world-space origin.
  [[nodiscard]] std::optional<assets::AssetVector3> viewmodel_attachment_at_frame(
      const game_api::ViewmodelIntent&, std::uint32_t attachment_index,
      double frame_coordinate) const;
  [[nodiscard]] bool has_shell_model() const noexcept { return shell_model_index_.has_value(); }
  [[nodiscard]] bool has_shell_model(std::string_view virtual_name) const noexcept;
  [[nodiscard]] ReplayLocalVisualStatus shell_resource_status() const noexcept {
    return shell_resource_status_;
  }
  [[nodiscard]] ReplayShellFrameStatus last_shell_frame_status() const noexcept {
    return last_shell_frame_status_;
  }
  [[nodiscard]] std::optional<renderer::RenderDynamicEntities>
  materialize_shells(std::span<const renderer::TransientShellState>, double now,
      std::span<const TemporaryStudioFrame> models = {});
  [[nodiscard]] std::optional<renderer::RenderDynamicEntities>
  materialize_temporary_sprites(std::span<const TemporarySpriteFrame>, double now);
  struct MuzzleSpriteFrame {
    assets::AssetVector3 origin{};
    game_api::MuzzleSpriteProfile sprite;
  };
  [[nodiscard]] std::optional<renderer::RenderDynamicEntities>
  materialize_muzzle_sprites(std::span<const MuzzleSpriteFrame>, double now);
  struct MuzzleSpriteStatistics {std::uint64_t ready{},missing{},rejected{},submitted{},unavailable{},frame_rejected{};};
  [[nodiscard]] const MuzzleSpriteStatistics& muzzle_sprite_statistics() const noexcept {return muzzle_sprite_stats_;}
  void consume_follow(const client::CommittedTemporaryEffect&,double,
      const game_api::GameClientHost* = nullptr) noexcept;
  void present_entity_effects(renderer::RenderScene&,double,std::span<const game_api::BeamSpriteFrame> = {},
      std::optional<std::uint32_t> receiving_entity={},std::optional<assets::AssetVector3> receiving_muzzle={});
  // Call after other effect-light producers. Consumes only this frame's staged
  // beam lights, retaining earlier muzzle/explosion/tracer light priority.
  void append_beam_lights(renderer::RenderScene&) noexcept;
  [[nodiscard]] const EntityFollowPresentation::Statistics& entity_effect_statistics() const noexcept {return entity_follow_.statistics();}
  struct FollowResourceStatistics {
    std::uint64_t submitted{}, absent{}, unsupported{}, frame_rejected{};
  };
  [[nodiscard]] const FollowResourceStatistics& follow_resource_statistics() const noexcept {return follow_resource_stats_;}
  struct TemporarySpriteStatistics {
    std::uint64_t submitted{}, absent{}, unsupported{}, invalid{}, expired{}, frame_rejected{};
  };
  [[nodiscard]] const TemporarySpriteStatistics& temporary_sprite_statistics() const noexcept {return temporary_sprite_stats_;}
  [[nodiscard]] TemporarySpriteStatistics temporary_sprite_statistics(std::uint8_t tag) const noexcept {
    return tag<temporary_sprite_tag_stats_.size() ? temporary_sprite_tag_stats_[tag] : TemporarySpriteStatistics{};
  }
  [[nodiscard]] std::optional<game_api::LocalWeaponModelMetadata>
  presentation_model(std::uint64_t generation, std::uint32_t model_index);
  // Immutable, retained metadata from an already approved/imported Studio slot.
  // No asset I/O or mutable engine state escapes this read view.
  [[nodiscard]] std::shared_ptr<const assets::SkeletalModelAssetData>
  studio_model_data(std::uint32_t model_index) const noexcept;
  [[nodiscard]] std::span<const game_api::RemoteWeaponPoseSample> remote_weapon_poses() const noexcept {
    return std::span{remote_weapon_poses_}.first(remote_weapon_pose_count_);
  }
  [[nodiscard]] ReplayLocalVisualStatus first_person_status() const noexcept {
    return first_person_status_;
  }

private:
  // Shares the retained-world probe across packet models, camera-local models
  // and the bounded transient pool. Keys 0 and 0xe0000000+ are local namespaces.
  [[nodiscard]] std::optional<std::array<float,3>> studio_light(
      std::uint32_t key, assets::AssetVector3 world_origin);
  mutable collision::CollisionQueryScratch effect_trace_scratch_;
  mutable std::shared_ptr<const goldsrc::collision::BrushCollisionModelLibrary> placement_collision_library_;
  mutable std::shared_ptr<const goldsrc::collision::BrushCollisionScene> impact_brush_scene_;
  mutable std::uint64_t impact_brush_generation_{},impact_brush_revision_{};
  std::shared_ptr<const client::RuntimeClientObservationState> placement_entities_;
  struct Binding {
    ReplayLocalVisualStatus status{ReplayLocalVisualStatus::unsupported_asset};
    std::optional<std::size_t> library_index;
    std::optional<std::size_t> render_index;
    std::optional<std::uint32_t> brush_model;
    assets::AssetVector3 brush_source_origin{};
  };
  ReplayLocalVisualSummary summary_;
  goldsrc::PreparedSoundResources sound_resources_;
  goldsrc::PreparedSkyAssets sky_assets_;
  std::shared_ptr<goldsrc::ApprovedPlayerModelAssets> player_model_assets_;
  struct AppearanceVariant {
    std::string key, virtual_name;
    std::shared_ptr<const assets::ModelAsset> model;
    assets::AssetSourceFingerprint fingerprint;
    entity_render::EntityRenderResourceIdentity identity;
    std::size_t render_index{};
    bool remapped{};
  };
  std::vector<AppearanceVariant> appearance_variants_; // max64, no per-frame uploads
  std::uint64_t appearance_variant_bytes_{};
  struct PublishedAppearance {std::uint64_t occupant_epoch{}; std::size_t variant{};};
  std::map<std::uint32_t,PublishedAppearance> published_appearances_; // at most32 slots
  struct CorpseAppearance {
    client::RuntimePacketEntityObservation anchor;
    game_api::WorldEntityPresentationIntent selection;
    game_api::PlayerAppearanceSelection appearance;
  };
  std::map<std::uint32_t,CorpseAppearance> corpse_appearances_; // max64 retained body identities
  [[nodiscard]] std::optional<std::size_t> resolve_player_appearance(
      game_api::PlayerAppearanceSelection&,const assets::ModelAsset&,
      entity_render::EntityRenderResourceIdentity,assets::AssetSourceFingerprint,
      const client::RuntimePacketEntityObservation&);
  std::shared_ptr<const goldsrc::PreparedDecalAssets> surface_decal_assets_;
  std::array<game_api::LocalDecalMaterialMode,game_api::kMaximumPreparedDecalAssets> surface_decal_modes_{};
  std::vector<WorldImpactPresentation::ServerDecalTargetLifetime> server_decal_lifetimes_;
  std::array<LocalImpactDecalStatus,2U> impact_decal_statuses_{
      LocalImpactDecalStatus::not_requested,LocalImpactDecalStatus::not_requested};
  std::array<std::shared_ptr<const assets::WorldTextureAsset>,2U> impact_decal_textures_{};
  std::array<game_api::LocalDecalMaterialMode,2U> impact_decal_modes_{};
  std::string movement_materials_;
  game_api::EffectPaletteMetadata effect_palette_;
  std::map<std::uint32_t, Binding> bindings_;
  std::map<std::string,std::size_t,std::less<>> effect_sprite_bindings_;
  MuzzleSpriteStatistics muzzle_sprite_stats_;
  std::uint64_t muzzle_sprite_frame_revision_{};
  goldsrc::studio::StudioBoneMergeCache bone_merge_cache_;
  std::map<std::uint32_t, game_api::LocalWeaponModelMetadata> presentation_models_;
  std::uint32_t maximum_clients_{};
  std::shared_ptr<const client::RuntimeClientObservationState> previous_entities_, current_entities_;
  goldsrc::RuntimeOrdinarySnapshotPresentation ordinary_presentation_;
  goldsrc::RuntimePlayerSnapshotPresentation player_presentation_;
  goldsrc::studio::StudioEntityAnimationPresentation ordinary_animation_;
  core::MultiplayerDiagnosticSink* diagnostic_sink_{};
  std::uint64_t diagnostic_map_generation_{}, ordinary_diagnostic_serial_{};
  struct OrdinaryDiagnosticSample {
    bool used{}, cache_hit{};
    std::uint32_t entity{}, selected_sequence{};
    std::optional<std::uint32_t> raw_sequence;
    std::uint64_t generation{}, lifetime{}, model_resource{}, observed_ns{};
    std::uint32_t suppressed{};
  };
  std::array<OrdinaryDiagnosticSample,128> ordinary_diagnostic_samples_{};
  std::size_t ordinary_diagnostic_next_{};
  std::uint32_t ordinary_diagnostic_evictions_{};
  void observe_ordinary_studio(std::uint32_t entity, std::uint64_t model_resource,
      const client::RuntimePacketEntityObservation* raw,
      const client::RuntimeClientObservationState* source,
      const goldsrc::studio::StudioPoseInput&, assets::AssetVector3 origin,
      assets::AssetVector3 angles, bool cache_hit,
      std::optional<double> sample_seconds,
      std::optional<double> previous_source_seconds) noexcept;
  std::vector<goldsrc::ReferenceBrushCollisionContext> remote_support_contexts_; // max17, cached per publication
  std::shared_ptr<const goldsrc::collision::BrushCollisionScene> sampled_brush_scene_;
  collision::CollisionQueryScratch remote_support_scratch_;
  std::shared_ptr<const client::RuntimeClientObservationState> sampled_player_previous_, sampled_player_current_;
  std::vector<goldsrc::RuntimePlayerInterpolationAnchor> sampled_player_anchors_;
  std::vector<goldsrc::RuntimePlayerAnimationEffectException> sampled_motion_exceptions_; // max16x32
  std::vector<RemoteMovementObservation> remote_movement_; // opt-in, at most32
  bool remote_movement_valid_{};
  void capture_remote_movement(const client::RuntimeClientObservationState&,
      const client::ClientWorldState&) noexcept;
  struct PlayerLifetime { std::uint64_t identity{}; std::optional<std::uint32_t> model; std::uint64_t continuity_floor{}; };
  struct AuthoritativeObservationStage {
    bool retain{};
    std::shared_ptr<const client::RuntimeClientObservationState> retained;
    std::optional<goldsrc::RuntimeOrdinarySnapshotPresentation> ordinary;
    std::optional<goldsrc::RuntimePlayerSnapshotPresentation> players;
    std::map<std::uint32_t, PlayerLifetime> lifetimes;
    std::uint64_t lifetime_identity{};
    std::unique_ptr<EntityFollowPresentation> follow;
    std::vector<client::RuntimePacketEntityObservation> effect_entities;
    std::vector<WorldImpactPresentation::ServerDecalTargetLifetime> decal_lifetimes;
  };
  [[nodiscard]] AuthoritativeObservationStage stage_authoritative_observation(
      const client::RuntimeClientObservationState&);
  void commit_authoritative_observation(AuthoritativeObservationStage&&) noexcept;
  std::map<std::uint32_t, PlayerLifetime> player_lifetimes_;
  std::uint64_t next_player_lifetime_{};
  // Receipt freshness for remote sockets only, not the buffered player clock.
  std::uint64_t clock_record_{};
  double clock_anchor_seconds_{};
  double sampled_server_seconds_{};
  double sampled_application_seconds_{};
  std::array<game_api::RemoteWeaponPoseSample,32> remote_weapon_poses_{};
  std::size_t remote_weapon_pose_count_{};
  double sampled_previous_seconds_{}, sampled_current_seconds_{}, sampled_alpha_{};
  std::optional<goldsrc::EntitySnapshotPairSelectionStatus> sampled_selection_status_;
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
  std::uint64_t temporary_sprite_frame_revision_{};
  TemporarySpriteStatistics temporary_sprite_stats_;
  std::array<TemporarySpriteStatistics,4> temporary_sprite_tag_stats_{};
  EntityFollowPresentation entity_follow_;
  std::vector<client::RuntimePacketEntityObservation> presented_effect_entities_;
  // Owning numeric availability decisions for the current presentation. Size
  // is bounded by the validated kMaximumRuntimeObservationEntities input; an
  // initially missing/unknown source is not an explicit rejection.
  std::vector<std::uint32_t> unavailable_effect_sources_;
  struct BeamEndpointBinding {
    client::RuntimeCustomBeamObservation::Type type{};
    client::RuntimeBeamEndpoint start{},end{};
    client::RuntimeVector3Observation point;
    std::uint32_t sprite{};
    std::optional<std::uint32_t> start_model,end_model;
    std::uint64_t start_player_life{},end_player_life{};
    bool start_bound{},end_bound{},broken{};
  };
  std::map<std::uint32_t,BeamEndpointBinding> beam_endpoint_bindings_; // max64
  std::map<std::uint32_t,game_api::BeamRibbonGlowProfile> beam_glow_profiles_; // approved sprite slots only
  std::map<std::uint32_t,std::array<game_api::BeamSceneLightProfile,2>> beam_light_profiles_;
  std::array<renderer::RenderPointLight,4> beam_lights_{};
  std::size_t beam_light_count_{};
  std::weak_ptr<const entity_render::EntityRenderFrame> beam_light_frame_;
  std::map<std::uint32_t,std::array<std::optional<assets::AssetVector3>,4>> presented_attachments_;
  std::uint64_t follow_frame_revision_{};
  FollowResourceStatistics follow_resource_stats_;
  std::optional<std::uint32_t> shell_model_index_;
  ReplayLocalVisualStatus shell_resource_status_{ReplayLocalVisualStatus::absent_model};
  ReplayShellFrameStatus last_shell_frame_status_{ReplayShellFrameStatus::not_attempted};
  std::optional<assets::AssetVector3> viewmodel_attachment_zero_;

  ReplayLocalVisualStatus first_person_status_{ReplayLocalVisualStatus::absent_model};
};
} // namespace hlclient::app

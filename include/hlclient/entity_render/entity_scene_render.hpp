#pragma once

#include <hlclient/entity_render/sprite_render_asset.hpp>
#include <hlclient/entity_render/studio_model_render_asset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::entity_visual {
class EntityVisualAssetLibraryState;
}

namespace hlclient::world_spatial {
class WorldSpatialPackage;
}

namespace hlclient::world_visibility {
class WorldViewFrustum;
}

namespace hlclient::entity_render {

enum class RuntimeEntityVisualKind {
    studio_model,
    sprite,
    unsupported,
};

enum class RuntimeEntityVisibilityStatus {
    visible,
    culled_by_pvs,
    culled_by_frustum,
    asset_unavailable,
    visual_projection_pending,
    unsupported_visual,
};

enum class StudioEntityMaterialSupportStatus {
    supported_opaque,
    supported_masked,
    supported_opaque_and_masked,
    unsupported_material,
};

struct StudioRenderPose {
    EntityRenderResourceIdentity model_resource_identity{};
    // Column-major affine matrices; these are CPU values, not renderer or GL
    // resources. The pose index on a Studio instance indexes this frame table.
    std::vector<std::array<float, 16U>> bone_matrices;
};

enum class StudioBlendMode : std::uint8_t { opaque, alpha_blended, additive };

struct StudioEntityRenderInstance {
    std::uint32_t entity_number{0U};
    std::uint32_t studio_asset_index{0U};
    std::uint32_t pose_index{0U};
    EntityRenderTransform transform{};
    std::uint32_t body_value{0U};
    std::uint32_t skin_family_index{0U};
    StudioEntityMaterialSupportStatus material_support_status{
        StudioEntityMaterialSupportStatus::supported_opaque};
    assets::WorldBounds interpolated_bounds{};
    RuntimeEntityVisibilityStatus visibility_status{
        RuntimeEntityVisibilityStatus::visible};
    // Neutral linear light multiplier sampled upstream from approved world
    // data. Absence preserves the established 0.85 Studio fallback.
    std::optional<std::array<float, 3U>> static_light_rgb;
    // Draw identity within the original server entity: 0 primary, 1 attached.
    // Attached candidates require a primary Studio owner, never a fake entity.
    std::uint32_t subinstance_ordinal{};
    // Neutral compositing; protocol render modes are translated upstream.
    StudioBlendMode blend_mode{StudioBlendMode::opaque};
    float opacity{1.0F};
    std::array<float, 3U> tint{1.0F, 1.0F, 1.0F};
};

enum class SpriteBlendMode : std::uint8_t {
    source_asset_default, opaque, additive, alpha_blended, glow
};

struct SpriteEntityRenderInstance {
    std::uint32_t entity_number{0U};
    std::uint32_t sprite_asset_index{0U};
    std::uint32_t selected_frame_index{0U};
    EntityRenderTransform transform{};
    assets::SpriteOrientation orientation{
        assets::SpriteOrientation::view_parallel};
    SpriteRenderTextureSupportStatus texture_format_support{
        SpriteRenderTextureSupportStatus::supported_normal_opaque};
    assets::WorldBounds bounds{};
    RuntimeEntityVisibilityStatus visibility_status{
        RuntimeEntityVisibilityStatus::visible};
    // Neutral per-instance execution overrides for finite transient effects.
    SpriteBlendMode blend_mode{SpriteBlendMode::source_asset_default};
    float opacity{1.0F};
    std::array<float,3> tint{1,1,1};
    // Neutral textured ribbon: endpoint/width in world units, no game profile.
    std::optional<assets::AssetVector3> ribbon_end;
    float ribbon_width{1};
    float ribbon_noise_amplitude{},ribbon_scroll_cycles_per_second{};
    bool ribbon_sine_noise{};
    double ribbon_phase_seconds{};
    // Optional same-centerline additive halo. Core width/UV/noise stay exact.
    float ribbon_glow_width_scale{1},ribbon_glow_opacity{};
    // Explicit neutral surface attachment: only an additive, non-ribbon
    // view-parallel source may execute with an oriented instance transform.
    // The source asset metadata remains unchanged and authoritative otherwise.
    bool surface_aligned{false};
};

enum class UnsupportedEntityVisualReason {
    missing_asset,
    projection_evidence_pending,
    unsupported_asset_kind,
    unsupported_material,
    unsupported_sprite_format,
    unsupported_sprite_orientation,
    unsupported_render_mode,
};

struct UnsupportedEntityVisualInstance {
    std::uint32_t entity_number{0U};
    std::optional<std::uint32_t> visual_asset_index;
    UnsupportedEntityVisualReason reason{
        UnsupportedEntityVisualReason::unsupported_asset_kind};
    RuntimeEntityVisibilityStatus visibility_status{
        RuntimeEntityVisibilityStatus::unsupported_visual};
};

enum class EntityDrawClass {
    studio_opaque,
    studio_masked,
    sprite_normal,
    sprite_alpha_test,
};

struct EntityDrawCommand {
    EntityDrawClass draw_class{EntityDrawClass::studio_opaque};
    RuntimeEntityVisualKind visual_kind{RuntimeEntityVisualKind::studio_model};
    std::uint32_t entity_number{0U};
    // Index into the matching ordered instance span on EntityRenderFrame.
    std::uint32_t instance_index{0U};
};

struct EntitySceneRenderStatistics {
    std::size_t visual_asset_count{0U};
    std::size_t studio_asset_count{0U};
    std::size_t sprite_asset_count{0U};
    std::size_t studio_vertex_count{0U};
    std::size_t studio_index_count{0U};
    std::size_t studio_mesh_count{0U};
    std::size_t sprite_frame_count{0U};
    std::size_t model_gpu_source_bytes{0U};
    std::size_t sprite_gpu_source_bytes{0U};
};

enum class EntitySceneRenderErrorCode {
    invalid_configuration,
    missing_asset_library,
    invalid_library_identity,
    invalid_resource_identity,
    invalid_studio_asset,
    invalid_sprite_asset,
    duplicate_asset_identity,
    source_limit_exceeded,
    unable_to_retain_scene,
};

[[nodiscard]] std::string_view to_string(
    EntitySceneRenderErrorCode code) noexcept;

struct EntitySceneRenderError {
    EntitySceneRenderErrorCode code{
        EntitySceneRenderErrorCode::invalid_configuration};
    std::optional<std::size_t> element_index;
    std::string context;
};

class EntitySceneRenderPackage;

enum class EntitySceneRenderAssetSource {
    imported_asset_library,
    project_generated_diagnostic,
};

struct EntitySceneRenderPackageCreateInfo {
    struct PresentationStudioSource {
        EntityRenderResourceIdentity identity;
        std::shared_ptr<const assets::ModelAsset> model;
    };
    struct PresentationSpriteSource {
        EntityRenderResourceIdentity identity;
        std::shared_ptr<const assets::SpriteAsset> sprite;
    };
    EntitySceneRenderAssetSource asset_source{
        EntitySceneRenderAssetSource::imported_asset_library};
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
        asset_library;
    EntityRenderResourceIdentity asset_library_identity{};
    std::uint64_t resource_id{0U};
    std::optional<EntityRenderResourceIdentity> world_scene_association;
    std::vector<std::shared_ptr<const StudioModelRenderAsset>> studio_assets;
    std::vector<std::shared_ptr<const SpriteRenderAsset>> sprite_assets;
    // Already imported owning client-presentation dependencies. They are not
    // manifest entries and never acquire a fictitious network model slot.
    std::vector<PresentationStudioSource> presentation_studio_sources;
    std::vector<PresentationSpriteSource> presentation_sprite_sources;
};

class EntitySceneRenderPackage final {
public:
    EntitySceneRenderPackage(const EntitySceneRenderPackage&) = delete;
    EntitySceneRenderPackage& operator=(const EntitySceneRenderPackage&) = delete;
    EntitySceneRenderPackage(EntitySceneRenderPackage&&) noexcept = default;
    EntitySceneRenderPackage& operator=(EntitySceneRenderPackage&&) = delete;
    ~EntitySceneRenderPackage() = default;

    [[nodiscard]] const std::shared_ptr<
        const entity_visual::EntityVisualAssetLibraryState>& asset_library()
        const noexcept;
    [[nodiscard]] EntityRenderResourceIdentity asset_library_identity()
        const noexcept;
    [[nodiscard]] EntitySceneRenderAssetSource asset_source() const noexcept;
    [[nodiscard]] std::span<const EntitySceneRenderPackageCreateInfo::PresentationStudioSource>
    presentation_studio_sources() const noexcept { return presentation_studio_sources_; }
    [[nodiscard]] std::span<const EntitySceneRenderPackageCreateInfo::PresentationSpriteSource>
    presentation_sprite_sources() const noexcept { return presentation_sprite_sources_; }
    [[nodiscard]] std::span<
        const std::shared_ptr<const StudioModelRenderAsset>> studio_assets()
        const noexcept;
    [[nodiscard]] std::span<
        const std::shared_ptr<const SpriteRenderAsset>> sprite_assets()
        const noexcept;
    [[nodiscard]] std::optional<EntityRenderResourceIdentity>
    world_scene_association() const noexcept;
    [[nodiscard]] const EntitySceneRenderStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;

private:
    friend class EntitySceneRenderPackageBuilder;

    EntitySceneRenderPackage(
        EntitySceneRenderAssetSource asset_source,
        std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
            asset_library,
        EntityRenderResourceIdentity asset_library_identity,
        std::uint64_t resource_id,
        std::uint64_t resource_revision,
        std::optional<EntityRenderResourceIdentity> world_scene_association,
        std::vector<std::shared_ptr<const StudioModelRenderAsset>> studio_assets,
        std::vector<std::shared_ptr<const SpriteRenderAsset>> sprite_assets,
        std::vector<EntitySceneRenderPackageCreateInfo::PresentationStudioSource> presentation_studio_sources,
        std::vector<EntitySceneRenderPackageCreateInfo::PresentationSpriteSource> presentation_sprite_sources,
        EntitySceneRenderStatistics statistics) noexcept;

    EntitySceneRenderAssetSource asset_source_{
        EntitySceneRenderAssetSource::imported_asset_library};
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
        asset_library_;
    EntityRenderResourceIdentity asset_library_identity_{};
    std::uint64_t resource_id_{0U};
    std::uint64_t resource_revision_{0U};
    std::optional<EntityRenderResourceIdentity> world_scene_association_;
    std::vector<std::shared_ptr<const StudioModelRenderAsset>> studio_assets_;
    std::vector<std::shared_ptr<const SpriteRenderAsset>> sprite_assets_;
    std::vector<EntitySceneRenderPackageCreateInfo::PresentationStudioSource> presentation_studio_sources_;
    std::vector<EntitySceneRenderPackageCreateInfo::PresentationSpriteSource> presentation_sprite_sources_;
    EntitySceneRenderStatistics statistics_{};
};

struct EntitySceneRenderPackageBuildResult {
    std::optional<EntitySceneRenderPackage> package;
    std::optional<EntitySceneRenderError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return package.has_value();
    }
};

class EntitySceneRenderPackageBuilder final {
public:
    [[nodiscard]] EntitySceneRenderPackageBuildResult build(
        EntitySceneRenderPackageCreateInfo create_info,
        const RuntimeEntityVisualLimits& limits = {}) const;
};

enum class EntityRenderInterpolationProfile {
    synthetic_seconds_v1,
    decoded_discrete_runtime_replay_v1,
    stock_server_time_evidence_pending,
    // Public runtime svc_time observations selected upstream, without claiming
    // synthetic fixture provenance or stock-binary interpolation parity.
    public_runtime_server_seconds_v1,
};

struct EntityRenderInterpolationMetadata {
    double sample_time_seconds{0.0};
    double previous_time_seconds{0.0};
    double current_time_seconds{0.0};
    float alpha{0.0F};
    std::uint64_t previous_state_identity{0U};
    std::uint64_t current_state_identity{0U};
    EntityRenderInterpolationProfile profile{
        EntityRenderInterpolationProfile::synthetic_seconds_v1};
};

struct EntityRenderFrameStatistics {
    std::size_t candidate_count{0U};
    std::size_t visible_count{0U};
    std::size_t studio_instance_count{0U};
    std::size_t sprite_instance_count{0U};
    std::size_t unsupported_instance_count{0U};
    std::size_t pose_count{0U};
    std::size_t total_bone_matrix_count{0U};
    std::size_t culled_by_pvs_count{0U};
    std::size_t culled_by_frustum_count{0U};
    std::size_t unavailable_count{0U};
    std::size_t projection_pending_count{0U};
    std::size_t unsupported_visual_count{0U};
    std::size_t draw_count{0U};
};

enum class EntityRenderFrameErrorCode {
    invalid_configuration,
    invalid_resource_identity,
    invalid_interpolation_metadata,
    invalid_pose,
    invalid_instance,
    duplicate_entity_number,
    invalid_asset_reference,
    invalid_pose_reference,
    invalid_world_spatial_context,
    spatial_query_failed,
    source_limit_exceeded,
    unable_to_retain_frame,
};

[[nodiscard]] std::string_view to_string(
    EntityRenderFrameErrorCode code) noexcept;

struct EntityRenderFrameError {
    EntityRenderFrameErrorCode code{
        EntityRenderFrameErrorCode::invalid_configuration};
    std::optional<std::uint32_t> entity_number;
    std::string context;
};

struct EntityRenderFrameBuildInput {
    std::uint64_t resource_id{0U};
    std::uint64_t resource_revision{0U};
    EntityRenderInterpolationMetadata interpolation{};
    std::vector<StudioRenderPose> studio_poses;
    std::vector<StudioEntityRenderInstance> studio_instances;
    std::vector<SpriteEntityRenderInstance> sprite_instances;
    std::vector<UnsupportedEntityVisualInstance> unsupported_instances;
    const world_visibility::WorldViewFrustum* view_frustum{nullptr};
    const world_spatial::WorldSpatialPackage* spatial_package{nullptr};
    std::optional<std::uint32_t> camera_leaf_index;
    // Project diagnostic seam: a caller may retain one already-visible
    // synthetic camera-anchor candidate while applying camera-derived PVS and
    // frustum culling to every other entity. This is not stock self-rendering.
    std::optional<std::uint32_t> camera_cull_exempt_entity_number;
};

class EntityRenderFrame final {
public:
    EntityRenderFrame(const EntityRenderFrame&) = delete;
    EntityRenderFrame& operator=(const EntityRenderFrame&) = delete;
    EntityRenderFrame(EntityRenderFrame&&) noexcept = default;
    EntityRenderFrame& operator=(EntityRenderFrame&&) = delete;
    ~EntityRenderFrame() = default;

    [[nodiscard]] const EntityRenderInterpolationMetadata& interpolation()
        const noexcept;
    [[nodiscard]] EntityRenderResourceIdentity scene_package_identity()
        const noexcept;
    [[nodiscard]] std::span<const StudioRenderPose> studio_poses() const noexcept;
    [[nodiscard]] std::span<const StudioEntityRenderInstance> studio_instances()
        const noexcept;
    [[nodiscard]] std::span<const SpriteEntityRenderInstance> sprite_instances()
        const noexcept;
    [[nodiscard]] std::span<const UnsupportedEntityVisualInstance>
    unsupported_instances() const noexcept;
    [[nodiscard]] std::span<const EntityDrawCommand> draw_commands() const noexcept;
    [[nodiscard]] const EntityRenderFrameStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t resource_id() const noexcept;
    [[nodiscard]] std::uint64_t resource_revision() const noexcept;
    [[nodiscard]] std::uint64_t frame_signature() const noexcept;

private:
    friend class EntityRenderFrameBuilder;

    EntityRenderFrame(
        EntityRenderResourceIdentity scene_package_identity,
        std::uint64_t resource_id,
        std::uint64_t resource_revision,
        std::uint64_t frame_signature,
        EntityRenderInterpolationMetadata interpolation,
        std::vector<StudioRenderPose> studio_poses,
        std::vector<StudioEntityRenderInstance> studio_instances,
        std::vector<SpriteEntityRenderInstance> sprite_instances,
        std::vector<UnsupportedEntityVisualInstance> unsupported_instances,
        std::vector<EntityDrawCommand> draw_commands,
        EntityRenderFrameStatistics statistics) noexcept;

    EntityRenderResourceIdentity scene_package_identity_{};
    std::uint64_t resource_id_{0U};
    std::uint64_t resource_revision_{0U};
    std::uint64_t frame_signature_{0U};
    EntityRenderInterpolationMetadata interpolation_{};
    std::vector<StudioRenderPose> studio_poses_;
    std::vector<StudioEntityRenderInstance> studio_instances_;
    std::vector<SpriteEntityRenderInstance> sprite_instances_;
    std::vector<UnsupportedEntityVisualInstance> unsupported_instances_;
    std::vector<EntityDrawCommand> draw_commands_;
    EntityRenderFrameStatistics statistics_{};
};

struct EntityRenderFrameBuildResult {
    std::optional<EntityRenderFrame> frame;
    std::optional<EntityRenderFrameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return frame.has_value();
    }
};

class EntityRenderFrameBuilder final {
public:
    [[nodiscard]] EntityRenderFrameBuildResult build(
        const EntitySceneRenderPackage& scene_package,
        EntityRenderFrameBuildInput input,
        const RuntimeEntityVisualLimits& limits = {}) const;
};

struct EntitySnapshotHistoryReference {
    std::uint64_t resource_id{0U};
    std::uint64_t revision{0U};
};

enum class RuntimeEntitySceneCompatibilityProfile {
    synthetic_entity_snapshot_playback_v1,
};

enum class RuntimeEntitySceneEvidenceProfile {
    project_owned_synthetic_projection,
    stock_visual_projection_evidence_pending,
};

struct RuntimeEntitySceneState {
    EntitySnapshotHistoryReference snapshot_history_reference{};
    std::shared_ptr<const entity_visual::EntityVisualAssetLibraryState>
        asset_library;
    std::shared_ptr<const EntitySceneRenderPackage> scene_package;
    std::shared_ptr<const EntityRenderFrame> current_frame;
    std::optional<EntityRenderResourceIdentity> world_scene_association;
    RuntimeEntitySceneCompatibilityProfile compatibility_profile{
        RuntimeEntitySceneCompatibilityProfile::
            synthetic_entity_snapshot_playback_v1};
    RuntimeEntitySceneEvidenceProfile evidence_profile{
        RuntimeEntitySceneEvidenceProfile::project_owned_synthetic_projection};
};

} // namespace hlclient::entity_render

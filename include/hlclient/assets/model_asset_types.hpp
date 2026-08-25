#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hlclient::assets {

// Renderer-neutral skeletal data. ModelAsset includes this header additively at
// its composition boundary; none of these records contains renderer, GPU, or
// runtime-entity state.
enum class ModelCoordinateSpace {
    source_native_goldsrc_z_up,
};

enum class ModelSkeletalCompatibilityProfile {
    goldsrc_studio_v10,
};

enum class ModelSkeletalEvidenceProfile {
    public_valve_wire_profile,
};

struct ModelBounds {
    AssetVector3 minimum{};
    AssetVector3 maximum{};
};

struct ModelBone {
    std::string name;
    std::int32_t parent_index{-1};
    std::uint32_t source_flags{0U};
    std::array<std::int32_t, 6U> controller_indices{
        -1, -1, -1, -1, -1, -1};
    AssetVector3 default_translation{};
    AssetVector3 default_rotation_radians{};
    std::array<float, 6U> source_scales{};
};

struct ModelBoneController {
    std::int32_t bone_index{-1};
    std::uint32_t source_type{0U};
    float start{0.0F};
    float end{0.0F};
    std::int32_t rest{0};
    std::int32_t controller_index{0};
    bool wraps_shortest_distance{false};
};

struct ModelHitbox {
    std::uint32_t bone_index{0U};
    std::int32_t group{0};
    ModelBounds local_bounds{};
};

struct ModelAttachment {
    std::string name;
    std::int32_t source_type{0};
    std::uint32_t bone_index{0U};
    AssetVector3 local_origin{};
    std::array<AssetVector3, 3U> local_vectors{};
};

struct ModelSkinnedVertex {
    AssetVector3 source_position{};
    AssetVector3 source_normal{};
    std::int16_t raw_texture_s{0};
    std::int16_t raw_texture_t{0};
    std::uint32_t position_bone_index{0U};
    std::uint32_t normal_bone_index{0U};
};

struct ModelMesh {
    std::uint32_t first_index{0U};
    std::uint32_t index_count{0U};
    std::uint32_t skin_reference_slot{0U};
    std::uint32_t source_mesh_ordinal{0U};
    std::uint32_t source_triangle_count{0U};
    std::uint32_t source_command_count{0U};
    // GoldSrc command streams and declared triangle counts retain compiler-
    // emitted degenerate geometry. The explicit index range includes these
    // triangles; this count makes that source evidence visible to consumers.
    std::uint32_t retained_degenerate_triangle_count{0U};
};

struct ModelSubmodel {
    std::string name;
    float bounding_radius{0.0F};
    std::vector<ModelSkinnedVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ModelMesh> meshes;
    ModelBounds bounds{};
    std::uint32_t source_model_ordinal{0U};
};

struct ModelBodyPart {
    std::string name;
    std::int32_t base{0};
    std::vector<std::uint32_t> submodel_indices;
};

enum class ModelTextureAlphaMode {
    opaque,
    masked_index_255,
    source_metadata_only,
};

struct ModelTextureAsset {
    std::string source_name;
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::vector<std::byte> rgba8_level_zero;
    std::vector<std::uint8_t> indexed_pixels;
    std::array<std::array<std::uint8_t, 3U>, 256U> palette_rgb{};
    std::uint32_t source_flags{0U};
    ModelTextureAlphaMode alpha_mode{ModelTextureAlphaMode::opaque};
    ModelSkeletalCompatibilityProfile compatibility_profile{
        ModelSkeletalCompatibilityProfile::goldsrc_studio_v10};
    ModelSkeletalEvidenceProfile evidence_profile{
        ModelSkeletalEvidenceProfile::public_valve_wire_profile};
};

struct ModelSkinFamily {
    std::vector<std::uint16_t> texture_indices;
};

enum class ModelAnimationChannelSemantic : std::uint8_t {
    translation_x = 0U,
    translation_y = 1U,
    translation_z = 2U,
    rotation_x = 3U,
    rotation_y = 4U,
    rotation_z = 5U,
};

struct ModelAnimationRun {
    std::uint32_t first_frame{0U};
    std::uint8_t valid_value_count{0U};
    std::uint8_t total_frame_count{0U};
    std::vector<std::int16_t> quantized_values;
};

struct ModelAnimationChannel {
    ModelAnimationChannelSemantic semantic{
        ModelAnimationChannelSemantic::translation_x};
    std::vector<ModelAnimationRun> runs;
    std::uint32_t frame_coverage{0U};
    float source_default{0.0F};
    float source_scale{0.0F};
    std::uint32_t source_sequence_group_ordinal{0U};
};

struct ModelBoneAnimationTrack {
    std::uint32_t bone_index{0U};
    std::array<ModelAnimationChannel, 6U> channels{};
};

struct ModelAnimationBlend {
    std::uint32_t source_blend_ordinal{0U};
    std::vector<ModelBoneAnimationTrack> bone_tracks;
};

struct ModelSequenceEvent {
    std::int32_t frame{0};
    std::int32_t event_number{0};
    std::int32_t source_type{0};
    std::vector<std::byte> options;
};

struct ModelSequencePivot {
    AssetVector3 origin{};
    std::int32_t start_frame{0};
    std::int32_t end_frame{0};
};

struct ModelSequence {
    std::string label;
    float frames_per_second{0.0F};
    std::uint32_t source_flags{0U};
    std::int32_t activity{0};
    std::int32_t activity_weight{0};
    std::uint32_t frame_count{0U};
    std::uint32_t blend_count{0U};
    std::array<std::int32_t, 2U> blend_types{};
    std::array<float, 2U> blend_start{};
    std::array<float, 2U> blend_end{};
    std::int32_t blend_parent{-1};
    std::int32_t motion_type{0};
    std::int32_t motion_bone{-1};
    AssetVector3 linear_movement{};
    ModelBounds bounds{};
    std::vector<ModelSequenceEvent> events;
    std::vector<ModelSequencePivot> pivots;
    std::uint32_t sequence_group_index{0U};
    std::vector<ModelAnimationBlend> animation_blends;
    std::int32_t entry_node{0};
    std::int32_t exit_node{0};
    std::int32_t node_flags{0};
    std::int32_t next_sequence{0};
};

struct ModelSequenceGroupMetadata {
    std::string label;
    // Informational source bytes only. This value is never a filesystem path.
    std::string untrusted_source_name;
    std::uint32_t source_group_ordinal{0U};
    bool externally_stored{false};
};

struct SkeletalModelStatistics {
    std::uint64_t source_count{0U};
    std::uint64_t bone_count{0U};
    std::uint64_t bodypart_count{0U};
    std::uint64_t submodel_count{0U};
    std::uint64_t mesh_count{0U};
    std::uint64_t emitted_vertex_count{0U};
    std::uint64_t emitted_triangle_count{0U};
    std::uint64_t retained_degenerate_triangle_count{0U};
    std::uint64_t texture_count{0U};
    std::uint64_t skin_family_count{0U};
    std::uint64_t sequence_count{0U};
    std::uint64_t sequence_group_count{0U};
    std::uint64_t animation_run_count{0U};
    std::uint64_t animation_value_bytes{0U};
};

struct SkeletalModelAssetData {
    ModelCoordinateSpace coordinate_space{
        ModelCoordinateSpace::source_native_goldsrc_z_up};
    ModelSkeletalCompatibilityProfile compatibility_profile{
        ModelSkeletalCompatibilityProfile::goldsrc_studio_v10};
    ModelSkeletalEvidenceProfile evidence_profile{
        ModelSkeletalEvidenceProfile::public_valve_wire_profile};
    AssetVector3 source_eye_position{};
    ModelBounds source_movement_bounds{};
    ModelBounds source_clipping_bounds{};
    std::uint32_t source_flags{0U};
    std::vector<ModelBone> bones;
    std::vector<ModelBoneController> bone_controllers;
    std::vector<ModelHitbox> hitboxes;
    std::vector<ModelAttachment> attachments;
    std::vector<ModelBodyPart> bodyparts;
    std::vector<ModelSubmodel> submodels;
    std::vector<ModelTextureAsset> textures;
    std::vector<ModelSkinFamily> skin_families;
    std::vector<ModelSequence> sequences;
    std::vector<ModelSequenceGroupMetadata> sequence_groups;
    std::vector<std::uint8_t> transition_table;
    std::uint32_t transition_node_count{0U};
    SkeletalModelStatistics statistics{};
};

} // namespace hlclient::assets

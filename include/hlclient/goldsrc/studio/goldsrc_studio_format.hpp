#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace hlclient::goldsrc::studio {

inline constexpr std::array<std::byte, 4U> kGoldSrcStudioIdentifier{
    std::byte{0x49}, std::byte{0x44}, std::byte{0x53}, std::byte{0x54}};
inline constexpr std::array<std::byte, 4U> kGoldSrcStudioSequenceIdentifier{
    std::byte{0x49}, std::byte{0x44}, std::byte{0x53}, std::byte{0x51}};
inline constexpr std::int32_t kGoldSrcStudioVersion = 10;

inline constexpr std::size_t kGoldSrcStudioHeaderWireSize = 244U;
inline constexpr std::size_t kGoldSrcStudioSequenceHeaderWireSize = 76U;
inline constexpr std::size_t kGoldSrcStudioBoneWireSize = 112U;
inline constexpr std::size_t kGoldSrcStudioBoneControllerWireSize = 24U;
inline constexpr std::size_t kGoldSrcStudioHitboxWireSize = 32U;
inline constexpr std::size_t kGoldSrcStudioSequenceGroupWireSize = 104U;
inline constexpr std::size_t kGoldSrcStudioSequenceWireSize = 176U;
inline constexpr std::size_t kGoldSrcStudioSequenceEventWireSize = 76U;
inline constexpr std::size_t kGoldSrcStudioPivotWireSize = 20U;
inline constexpr std::size_t kGoldSrcStudioAttachmentWireSize = 88U;
inline constexpr std::size_t kGoldSrcStudioAnimationOffsetWireSize = 12U;
inline constexpr std::size_t kGoldSrcStudioBodyPartWireSize = 76U;
inline constexpr std::size_t kGoldSrcStudioTextureWireSize = 80U;
inline constexpr std::size_t kGoldSrcStudioSubmodelWireSize = 112U;
inline constexpr std::size_t kGoldSrcStudioMeshWireSize = 20U;
inline constexpr std::size_t kGoldSrcStudioTriangleCommandVertexWireSize = 8U;
inline constexpr std::size_t kGoldSrcStudioSkinReferenceWireSize = 2U;
inline constexpr std::size_t kGoldSrcStudioSourceVertexWireSize = 12U;
inline constexpr std::size_t kGoldSrcStudioSourceNormalWireSize = 12U;

inline constexpr std::size_t kGoldSrcStudioHeaderNameOffset = 8U;
inline constexpr std::size_t kGoldSrcStudioHeaderLengthOffset = 72U;
inline constexpr std::size_t kGoldSrcStudioHeaderEyePositionOffset = 76U;
inline constexpr std::size_t kGoldSrcStudioHeaderMovementMinimumOffset = 88U;
inline constexpr std::size_t kGoldSrcStudioHeaderMovementMaximumOffset = 100U;
inline constexpr std::size_t kGoldSrcStudioHeaderClippingMinimumOffset = 112U;
inline constexpr std::size_t kGoldSrcStudioHeaderClippingMaximumOffset = 124U;
inline constexpr std::size_t kGoldSrcStudioHeaderFlagsOffset = 136U;
inline constexpr std::size_t kGoldSrcStudioHeaderBonesOffset = 140U;
inline constexpr std::size_t kGoldSrcStudioHeaderBoneControllersOffset = 148U;
inline constexpr std::size_t kGoldSrcStudioHeaderHitboxesOffset = 156U;
inline constexpr std::size_t kGoldSrcStudioHeaderSequencesOffset = 164U;
inline constexpr std::size_t kGoldSrcStudioHeaderSequenceGroupsOffset = 172U;
inline constexpr std::size_t kGoldSrcStudioHeaderTexturesOffset = 180U;
inline constexpr std::size_t kGoldSrcStudioHeaderTextureDataOffset = 188U;
inline constexpr std::size_t kGoldSrcStudioHeaderSkinReferencesOffset = 192U;
inline constexpr std::size_t kGoldSrcStudioHeaderBodyPartsOffset = 204U;
inline constexpr std::size_t kGoldSrcStudioHeaderAttachmentsOffset = 212U;
inline constexpr std::size_t kGoldSrcStudioHeaderSoundTableOffset = 220U;
inline constexpr std::size_t kGoldSrcStudioHeaderSoundIndexOffset = 224U;
inline constexpr std::size_t kGoldSrcStudioHeaderSoundGroupsOffset = 228U;
inline constexpr std::size_t kGoldSrcStudioHeaderSoundGroupOffset = 232U;
inline constexpr std::size_t kGoldSrcStudioHeaderTransitionsOffset = 236U;

inline constexpr std::uint32_t kGoldSrcStudioTextureFlatshade = 0x0001U;
inline constexpr std::uint32_t kGoldSrcStudioTextureChrome = 0x0002U;
inline constexpr std::uint32_t kGoldSrcStudioTextureFullbright = 0x0004U;
inline constexpr std::uint32_t kGoldSrcStudioTextureNoMips = 0x0008U;
inline constexpr std::uint32_t kGoldSrcStudioTextureAlpha = 0x0010U;
inline constexpr std::uint32_t kGoldSrcStudioTextureAdditive = 0x0020U;
inline constexpr std::uint32_t kGoldSrcStudioTextureMasked = 0x0040U;
inline constexpr std::uint32_t kGoldSrcStudioKnownTextureFlags = 0x007FU;
inline constexpr std::uint32_t kGoldSrcStudioControllerWrap = 0x8000U;
inline constexpr std::uint32_t kGoldSrcStudioControllerTypes = 0x003FU;

struct GoldSrcStudioCountOffset {
    std::int32_t count{0};
    std::int32_t offset{0};
};

struct GoldSrcStudioHeader {
    std::string name;
    std::int32_t declared_length{0};
    assets::AssetVector3 eye_position{};
    assets::AssetVector3 movement_minimum{};
    assets::AssetVector3 movement_maximum{};
    assets::AssetVector3 clipping_minimum{};
    assets::AssetVector3 clipping_maximum{};
    std::int32_t flags{0};
    GoldSrcStudioCountOffset bones{};
    GoldSrcStudioCountOffset bone_controllers{};
    GoldSrcStudioCountOffset hitboxes{};
    GoldSrcStudioCountOffset sequences{};
    GoldSrcStudioCountOffset sequence_groups{};
    GoldSrcStudioCountOffset textures{};
    std::int32_t texture_data_offset{0};
    std::int32_t skin_reference_count{0};
    std::int32_t skin_family_count{0};
    std::int32_t skin_offset{0};
    GoldSrcStudioCountOffset bodyparts{};
    GoldSrcStudioCountOffset attachments{};
    std::int32_t sound_table{0};
    std::int32_t sound_index{0};
    std::int32_t sound_group_count{0};
    std::int32_t sound_group_offset{0};
    std::int32_t transition_count{0};
    std::int32_t transition_offset{0};
};

struct GoldSrcStudioSequenceHeader {
    std::string name;
    std::int32_t declared_length{0};
};

struct GoldSrcStudioBoneRecord {
    std::string name;
    std::int32_t parent{-1};
    std::int32_t flags{0};
    std::array<std::int32_t, 6U> controllers{};
    std::array<float, 6U> values{};
    std::array<float, 6U> scales{};
};

struct GoldSrcStudioBoneControllerRecord {
    std::int32_t bone{-1};
    std::int32_t type{0};
    float start{0.0F};
    float end{0.0F};
    std::int32_t rest{0};
    std::int32_t index{0};
};

struct GoldSrcStudioHitboxRecord {
    std::int32_t bone{-1};
    std::int32_t group{0};
    assets::AssetVector3 minimum{};
    assets::AssetVector3 maximum{};
};

struct GoldSrcStudioSequenceGroupRecord {
    std::string label;
    std::string untrusted_name;
    std::int32_t unused1{0};
    std::int32_t unused2{0};
};

struct GoldSrcStudioSequenceRecord {
    std::string label;
    float fps{0.0F};
    std::int32_t flags{0};
    std::int32_t activity{0};
    std::int32_t activity_weight{0};
    GoldSrcStudioCountOffset events{};
    std::int32_t frame_count{0};
    GoldSrcStudioCountOffset pivots{};
    std::int32_t motion_type{0};
    std::int32_t motion_bone{-1};
    assets::AssetVector3 linear_movement{};
    std::int32_t automatic_movement_position_offset{0};
    std::int32_t automatic_movement_angle_offset{0};
    assets::AssetVector3 minimum{};
    assets::AssetVector3 maximum{};
    std::int32_t blend_count{0};
    std::int32_t animation_offset{0};
    std::array<std::int32_t, 2U> blend_types{};
    std::array<float, 2U> blend_start{};
    std::array<float, 2U> blend_end{};
    std::int32_t blend_parent{-1};
    std::int32_t sequence_group{0};
    std::int32_t entry_node{0};
    std::int32_t exit_node{0};
    std::int32_t node_flags{0};
    std::int32_t next_sequence{0};
};

struct GoldSrcStudioSequenceEventRecord {
    std::int32_t frame{0};
    std::int32_t event_number{0};
    std::int32_t type{0};
    std::array<std::byte, 64U> options{};
};

struct GoldSrcStudioPivotRecord {
    assets::AssetVector3 origin{};
    std::int32_t start{0};
    std::int32_t end{0};
};

struct GoldSrcStudioAttachmentRecord {
    std::string name;
    std::int32_t type{0};
    std::int32_t bone{-1};
    assets::AssetVector3 origin{};
    std::array<assets::AssetVector3, 3U> vectors{};
};

struct GoldSrcStudioAnimationOffsetRecord {
    std::array<std::uint16_t, 6U> channel_offsets{};
};

struct GoldSrcStudioBodyPartRecord {
    std::string name;
    std::int32_t model_count{0};
    std::int32_t base{0};
    std::int32_t model_offset{0};
};

struct GoldSrcStudioTextureRecord {
    std::string name;
    std::int32_t flags{0};
    std::int32_t width{0};
    std::int32_t height{0};
    std::int32_t data_offset{0};
};

struct GoldSrcStudioSubmodelRecord {
    std::string name;
    std::int32_t type{0};
    float bounding_radius{0.0F};
    GoldSrcStudioCountOffset meshes{};
    std::int32_t vertex_count{0};
    std::int32_t vertex_bone_offset{0};
    std::int32_t vertex_offset{0};
    std::int32_t normal_count{0};
    std::int32_t normal_bone_offset{0};
    std::int32_t normal_offset{0};
    GoldSrcStudioCountOffset groups{};
};

struct GoldSrcStudioMeshRecord {
    std::int32_t triangle_count{0};
    std::int32_t triangle_command_offset{0};
    std::int32_t skin_reference{0};
    std::int32_t normal_count{0};
    std::int32_t normal_offset{0};
};

struct GoldSrcStudioTriangleCommandVertex {
    std::int16_t vertex_index{0};
    std::int16_t normal_index{0};
    std::int16_t raw_s{0};
    std::int16_t raw_t{0};
};

class GoldSrcStudioWireDecoder final {
public:
    [[nodiscard]] static std::optional<GoldSrcStudioHeader> header(
        std::span<const std::byte> source);
    [[nodiscard]] static std::optional<GoldSrcStudioSequenceHeader> sequence_header(
        std::span<const std::byte> source);
    [[nodiscard]] static std::optional<GoldSrcStudioBoneRecord> bone(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioBoneControllerRecord> bone_controller(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioHitboxRecord> hitbox(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioSequenceGroupRecord> sequence_group(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioSequenceRecord> sequence(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioSequenceEventRecord> sequence_event(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioPivotRecord> pivot(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioAttachmentRecord> attachment(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioAnimationOffsetRecord> animation_offset(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioBodyPartRecord> bodypart(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioTextureRecord> texture(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioSubmodelRecord> submodel(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioMeshRecord> mesh(
        std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<GoldSrcStudioTriangleCommandVertex>
        triangle_command_vertex(
            std::span<const std::byte> source, std::size_t offset);
    [[nodiscard]] static std::optional<assets::AssetVector3> source_vector(
        std::span<const std::byte> source, std::size_t offset);
};

} // namespace hlclient::goldsrc::studio

#pragma once

#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::tests {

inline constexpr std::size_t kSyntheticStudioBoneOffset = 244U;
inline constexpr std::size_t kSyntheticStudioHitboxOffset = 356U;
inline constexpr std::size_t kSyntheticStudioAttachmentOffset = 388U;
inline constexpr std::size_t kSyntheticStudioAnimationOffset = 476U;
inline constexpr std::size_t kSyntheticStudioAnimationStreamOffset = 488U;
inline constexpr std::size_t kSyntheticStudioSequenceOffset = 492U;
inline constexpr std::size_t kSyntheticStudioSequenceGroupOffset = 668U;
inline constexpr std::size_t kSyntheticStudioBodyPartOffset = 772U;
inline constexpr std::size_t kSyntheticStudioSubmodelOffset = 848U;
inline constexpr std::size_t kSyntheticStudioMeshOffset = 960U;
inline constexpr std::size_t kSyntheticStudioVertexBonesOffset = 980U;
inline constexpr std::size_t kSyntheticStudioNormalBonesOffset = 983U;
inline constexpr std::size_t kSyntheticStudioVerticesOffset = 986U;
inline constexpr std::size_t kSyntheticStudioNormalsOffset = 1022U;
inline constexpr std::size_t kSyntheticStudioCommandsOffset = 1058U;
inline constexpr std::size_t kSyntheticStudioTextureOffset = 1086U;
inline constexpr std::size_t kSyntheticStudioSkinOffset = 1166U;
inline constexpr std::size_t kSyntheticStudioTextureDataOffset = 1168U;
inline constexpr std::size_t kSyntheticStudioSourceSize = 1937U;

inline void studio_write_u16le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value)
{
    bytes.at(offset) = std::byte{static_cast<std::uint8_t>(value & 0xFFU)};
    bytes.at(offset + 1U) =
        std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xFFU)};
}

inline void studio_write_i16le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::int16_t value)
{
    studio_write_u16le(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

inline void studio_write_u32le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.at(offset + index) = std::byte{static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(index * 8U)) & 0xFFU)};
    }
}

inline void studio_write_i32le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    studio_write_u32le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

inline void studio_write_f32le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const float value)
{
    studio_write_u32le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

inline void studio_write_fixed_string(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::size_t width,
    const std::string_view value)
{
    std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), width,
        std::byte{0});
    const auto count = std::min(width, value.size());
    for (std::size_t index = 0U; index < count; ++index) {
        bytes.at(offset + index) =
            std::byte{static_cast<std::uint8_t>(value[index])};
    }
}

inline void studio_write_vector3(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const float x,
    const float y,
    const float z)
{
    studio_write_f32le(bytes, offset, x);
    studio_write_f32le(bytes, offset + 4U, y);
    studio_write_f32le(bytes, offset + 8U, z);
}

[[nodiscard]] inline std::vector<std::byte> literal_minimal_goldsrc_studio_v10(
    const bool masked_texture = false)
{
    std::vector<std::byte> bytes(kSyntheticStudioSourceSize, std::byte{0});
    bytes[0U] = std::byte{0x49};
    bytes[1U] = std::byte{0x44};
    bytes[2U] = std::byte{0x53};
    bytes[3U] = std::byte{0x54};
    studio_write_i32le(bytes, 4U, 10);
    studio_write_fixed_string(bytes, 8U, 64U, "synthetic_minimal.mdl");
    studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    studio_write_vector3(bytes, 76U, 0.0F, 0.0F, 8.0F);
    studio_write_vector3(bytes, 88U, -1.0F, -1.0F, -1.0F);
    studio_write_vector3(bytes, 100U, 1.0F, 1.0F, 1.0F);
    studio_write_vector3(bytes, 112U, -1.0F, -1.0F, -1.0F);
    studio_write_vector3(bytes, 124U, 1.0F, 1.0F, 1.0F);
    studio_write_i32le(bytes, 140U, 1);
    studio_write_i32le(bytes, 144U, static_cast<std::int32_t>(kSyntheticStudioBoneOffset));
    studio_write_i32le(bytes, 148U, 0);
    studio_write_i32le(bytes, 152U, 0);
    studio_write_i32le(bytes, 156U, 1);
    studio_write_i32le(bytes, 160U, static_cast<std::int32_t>(kSyntheticStudioHitboxOffset));
    studio_write_i32le(bytes, 164U, 1);
    studio_write_i32le(bytes, 168U, static_cast<std::int32_t>(kSyntheticStudioSequenceOffset));
    studio_write_i32le(bytes, 172U, 1);
    studio_write_i32le(bytes, 176U,
        static_cast<std::int32_t>(kSyntheticStudioSequenceGroupOffset));
    studio_write_i32le(bytes, 180U, 1);
    studio_write_i32le(bytes, 184U,
        static_cast<std::int32_t>(kSyntheticStudioTextureOffset));
    studio_write_i32le(bytes, 188U,
        static_cast<std::int32_t>(kSyntheticStudioTextureDataOffset));
    studio_write_i32le(bytes, 192U, 1);
    studio_write_i32le(bytes, 196U, 1);
    studio_write_i32le(bytes, 200U, static_cast<std::int32_t>(kSyntheticStudioSkinOffset));
    studio_write_i32le(bytes, 204U, 1);
    studio_write_i32le(bytes, 208U,
        static_cast<std::int32_t>(kSyntheticStudioBodyPartOffset));
    studio_write_i32le(bytes, 212U, 1);
    studio_write_i32le(bytes, 216U,
        static_cast<std::int32_t>(kSyntheticStudioAttachmentOffset));

    studio_write_fixed_string(bytes, kSyntheticStudioBoneOffset, 32U, "root");
    studio_write_i32le(bytes, kSyntheticStudioBoneOffset + 32U, -1);
    for (std::size_t index = 0U; index < 6U; ++index) {
        studio_write_i32le(bytes, kSyntheticStudioBoneOffset + 40U + index * 4U, -1);
        studio_write_f32le(bytes, kSyntheticStudioBoneOffset + 88U + index * 4U,
            1.0F);
    }

    studio_write_i32le(bytes, kSyntheticStudioHitboxOffset, 0);
    studio_write_i32le(bytes, kSyntheticStudioHitboxOffset + 4U, 1);
    studio_write_vector3(bytes, kSyntheticStudioHitboxOffset + 8U,
        -1.0F, -1.0F, -1.0F);
    studio_write_vector3(bytes, kSyntheticStudioHitboxOffset + 20U,
        1.0F, 1.0F, 1.0F);

    studio_write_fixed_string(bytes, kSyntheticStudioAttachmentOffset, 32U, "tag");
    studio_write_i32le(bytes, kSyntheticStudioAttachmentOffset + 36U, 0);
    studio_write_vector3(bytes, kSyntheticStudioAttachmentOffset + 40U,
        0.0F, 0.0F, 0.0F);
    studio_write_vector3(bytes, kSyntheticStudioAttachmentOffset + 52U,
        1.0F, 0.0F, 0.0F);
    studio_write_vector3(bytes, kSyntheticStudioAttachmentOffset + 64U,
        0.0F, 1.0F, 0.0F);
    studio_write_vector3(bytes, kSyntheticStudioAttachmentOffset + 76U,
        0.0F, 0.0F, 1.0F);

    studio_write_u16le(bytes, kSyntheticStudioAnimationOffset, 12U);
    bytes[kSyntheticStudioAnimationStreamOffset] = std::byte{1};
    bytes[kSyntheticStudioAnimationStreamOffset + 1U] = std::byte{1};
    studio_write_i16le(bytes, kSyntheticStudioAnimationStreamOffset + 2U, 2);

    studio_write_fixed_string(bytes, kSyntheticStudioSequenceOffset, 32U, "idle");
    studio_write_f32le(bytes, kSyntheticStudioSequenceOffset + 32U, 30.0F);
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 56U, 1);
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 72U, 0);
    studio_write_vector3(bytes, kSyntheticStudioSequenceOffset + 96U,
        -1.0F, -1.0F, -1.0F);
    studio_write_vector3(bytes, kSyntheticStudioSequenceOffset + 108U,
        1.0F, 1.0F, 1.0F);
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 120U, 1);
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 124U,
        static_cast<std::int32_t>(kSyntheticStudioAnimationOffset));
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 152U, -1);

    studio_write_fixed_string(bytes, kSyntheticStudioSequenceGroupOffset,
        32U, "default");
    studio_write_fixed_string(bytes, kSyntheticStudioSequenceGroupOffset + 32U,
        64U, "models\\synthetic_minimal.mdl");

    studio_write_fixed_string(bytes, kSyntheticStudioBodyPartOffset, 64U, "body");
    studio_write_i32le(bytes, kSyntheticStudioBodyPartOffset + 64U, 1);
    studio_write_i32le(bytes, kSyntheticStudioBodyPartOffset + 68U, 1);
    studio_write_i32le(bytes, kSyntheticStudioBodyPartOffset + 72U,
        static_cast<std::int32_t>(kSyntheticStudioSubmodelOffset));

    studio_write_fixed_string(bytes, kSyntheticStudioSubmodelOffset, 64U, "mesh");
    studio_write_f32le(bytes, kSyntheticStudioSubmodelOffset + 68U, 2.0F);
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 72U, 1);
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 76U,
        static_cast<std::int32_t>(kSyntheticStudioMeshOffset));
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 80U, 3);
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 84U,
        static_cast<std::int32_t>(kSyntheticStudioVertexBonesOffset));
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 88U,
        static_cast<std::int32_t>(kSyntheticStudioVerticesOffset));
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 92U, 3);
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 96U,
        static_cast<std::int32_t>(kSyntheticStudioNormalBonesOffset));
    studio_write_i32le(bytes, kSyntheticStudioSubmodelOffset + 100U,
        static_cast<std::int32_t>(kSyntheticStudioNormalsOffset));

    studio_write_i32le(bytes, kSyntheticStudioMeshOffset, 1);
    studio_write_i32le(bytes, kSyntheticStudioMeshOffset + 4U,
        static_cast<std::int32_t>(kSyntheticStudioCommandsOffset));
    studio_write_i32le(bytes, kSyntheticStudioMeshOffset + 12U, 3);

    studio_write_vector3(bytes, kSyntheticStudioVerticesOffset,
        0.0F, 0.0F, 0.0F);
    studio_write_vector3(bytes, kSyntheticStudioVerticesOffset + 12U,
        1.0F, 0.0F, 0.0F);
    studio_write_vector3(bytes, kSyntheticStudioVerticesOffset + 24U,
        0.0F, 1.0F, 0.0F);
    for (std::size_t index = 0U; index < 3U; ++index) {
        studio_write_vector3(bytes, kSyntheticStudioNormalsOffset + index * 12U,
            0.0F, 0.0F, 1.0F);
    }
    studio_write_i16le(bytes, kSyntheticStudioCommandsOffset, 3);
    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto command = kSyntheticStudioCommandsOffset + 2U + index * 8U;
        studio_write_i16le(bytes, command, static_cast<std::int16_t>(index));
        studio_write_i16le(bytes, command + 2U, static_cast<std::int16_t>(index));
        studio_write_i16le(bytes, command + 4U,
            static_cast<std::int16_t>(index == 1U ? 16 : 0));
        studio_write_i16le(bytes, command + 6U,
            static_cast<std::int16_t>(index == 2U ? 16 : 0));
    }

    studio_write_fixed_string(bytes, kSyntheticStudioTextureOffset, 64U, "white");
    studio_write_i32le(bytes, kSyntheticStudioTextureOffset + 64U,
        masked_texture ? 0x40 : 0);
    studio_write_i32le(bytes, kSyntheticStudioTextureOffset + 68U, 1);
    studio_write_i32le(bytes, kSyntheticStudioTextureOffset + 72U, 1);
    studio_write_i32le(bytes, kSyntheticStudioTextureOffset + 76U,
        static_cast<std::int32_t>(kSyntheticStudioTextureDataOffset));
    studio_write_i16le(bytes, kSyntheticStudioSkinOffset, 0);
    bytes[kSyntheticStudioTextureDataOffset] =
        masked_texture ? std::byte{255} : std::byte{0};
    const auto palette = kSyntheticStudioTextureDataOffset + 1U;
    bytes[palette] = std::byte{10};
    bytes[palette + 1U] = std::byte{20};
    bytes[palette + 2U] = std::byte{30};
    bytes[palette + 255U * 3U] = std::byte{40};
    bytes[palette + 255U * 3U + 1U] = std::byte{50};
    bytes[palette + 255U * 3U + 2U] = std::byte{60};
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> synthetic_split_texture_main()
{
    auto bytes = literal_minimal_goldsrc_studio_v10();
    studio_write_i32le(bytes, 180U, 0);
    studio_write_i32le(bytes, 184U, 0);
    studio_write_i32le(bytes, 188U, 0);
    studio_write_i32le(bytes, 192U, 0);
    studio_write_i32le(bytes, 196U, 0);
    studio_write_i32le(bytes, 200U, 0);
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> synthetic_texture_companion()
{
    constexpr std::size_t texture_offset = 244U;
    constexpr std::size_t skin_offset = 324U;
    constexpr std::size_t data_offset = 326U;
    std::vector<std::byte> bytes(data_offset + 769U, std::byte{0});
    bytes[0U] = std::byte{0x49};
    bytes[1U] = std::byte{0x44};
    bytes[2U] = std::byte{0x53};
    bytes[3U] = std::byte{0x54};
    studio_write_i32le(bytes, 4U, 10);
    studio_write_fixed_string(bytes, 8U, 64U, "ignoredT.mdl");
    studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    studio_write_i32le(bytes, 180U, 1);
    studio_write_i32le(bytes, 184U, static_cast<std::int32_t>(texture_offset));
    studio_write_i32le(bytes, 188U, static_cast<std::int32_t>(data_offset));
    studio_write_i32le(bytes, 192U, 1);
    studio_write_i32le(bytes, 196U, 1);
    studio_write_i32le(bytes, 200U, static_cast<std::int32_t>(skin_offset));
    studio_write_fixed_string(bytes, texture_offset, 64U, "companion");
    studio_write_i32le(bytes, texture_offset + 68U, 1);
    studio_write_i32le(bytes, texture_offset + 72U, 1);
    studio_write_i32le(bytes, texture_offset + 76U,
        static_cast<std::int32_t>(data_offset));
    studio_write_i16le(bytes, skin_offset, 0);
    bytes[data_offset] = std::byte{0};
    bytes[data_offset + 1U] = std::byte{1};
    bytes[data_offset + 2U] = std::byte{2};
    bytes[data_offset + 3U] = std::byte{3};
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> synthetic_external_sequence_main()
{
    auto bytes = literal_minimal_goldsrc_studio_v10();
    const auto groups_offset = bytes.size();
    bytes.resize(bytes.size() + 2U *
        hlclient::goldsrc::studio::kGoldSrcStudioSequenceGroupWireSize);
    studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    studio_write_i32le(bytes, 172U, 2);
    studio_write_i32le(bytes, 176U, static_cast<std::int32_t>(groups_offset));
    studio_write_fixed_string(bytes, groups_offset, 32U, "default");
    studio_write_fixed_string(bytes, groups_offset + 104U, 32U, "group01");
    studio_write_fixed_string(bytes, groups_offset + 136U, 64U,
        "ignored\\header\\path01.mdl");
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 124U, 76);
    studio_write_i32le(bytes, kSyntheticStudioSequenceOffset + 156U, 1);
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> synthetic_sequence_group_01()
{
    std::vector<std::byte> bytes(92U, std::byte{0});
    bytes[0U] = std::byte{0x49};
    bytes[1U] = std::byte{0x44};
    bytes[2U] = std::byte{0x53};
    bytes[3U] = std::byte{0x51};
    studio_write_i32le(bytes, 4U, 10);
    studio_write_fixed_string(bytes, 8U, 64U, "ignored_group_name");
    studio_write_i32le(bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    studio_write_u16le(bytes, 76U, 12U);
    bytes[88U] = std::byte{1};
    bytes[89U] = std::byte{1};
    studio_write_i16le(bytes, 90U, 3);
    return bytes;
}

} // namespace hlclient::tests

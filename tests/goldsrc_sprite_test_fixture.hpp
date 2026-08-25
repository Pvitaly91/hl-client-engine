#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hlclient::tests::sprite_fixture {

inline constexpr std::size_t kHeaderSize = 40U;
inline constexpr std::size_t kPaletteCountOffset = kHeaderSize;
inline constexpr std::size_t kPaletteOffset = kPaletteCountOffset + 2U;
inline constexpr std::size_t kFirstTopLevelEntryOffset = kPaletteOffset + 256U * 3U;

struct FrameSpec {
    std::int32_t origin_x{0};
    std::int32_t origin_y{0};
    std::int32_t width{2};
    std::int32_t height{2};
    std::vector<std::byte> pixels{
        std::byte{0}, std::byte{1}, std::byte{0xFF}, std::byte{2}};
};

inline void append_u16_le(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(std::byte{static_cast<std::uint8_t>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xFFU)});
}

inline void append_u32_le(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.push_back(std::byte{static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(index * 8U)) & 0xFFU)});
    }
}

inline void append_i32_le(std::vector<std::byte>& bytes, const std::int32_t value)
{
    append_u32_le(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void append_f32_le(std::vector<std::byte>& bytes, const float value)
{
    append_u32_le(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void write_u32_le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = std::byte{static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(index * 8U)) & 0xFFU)};
    }
}

inline void write_i32_le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    write_u32_le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

inline void write_f32_le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const float value)
{
    write_u32_le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] inline std::vector<std::byte> literal_header_and_palette(
    const std::int32_t top_level_count,
    const std::int32_t texture_format = 0,
    const std::int32_t orientation = 2,
    const std::int32_t sync_type = 0,
    const std::int32_t maximum_width = 2,
    const std::int32_t maximum_height = 2)
{
    // This fixture is an independent direct wire-layout construction; no
    // production parser/serializer code participates in expected bytes.
    std::vector<std::byte> bytes(kHeaderSize, std::byte{0});
    write_u32_le(bytes, 0U, 0x50534449U); // little-endian "IDSP"
    write_i32_le(bytes, 4U, 2);
    write_i32_le(bytes, 8U, orientation);
    write_i32_le(bytes, 12U, texture_format);
    write_f32_le(bytes, 16U, 8.0F);
    write_i32_le(bytes, 20U, maximum_width);
    write_i32_le(bytes, 24U, maximum_height);
    write_i32_le(bytes, 28U, top_level_count);
    write_f32_le(bytes, 32U, 0.0F);
    write_i32_le(bytes, 36U, sync_type);

    append_u16_le(bytes, 256U);
    for (std::uint16_t index = 0U; index < 256U; ++index) {
        bytes.push_back(std::byte{static_cast<std::uint8_t>(index)});
        bytes.push_back(std::byte{static_cast<std::uint8_t>(255U - index)});
        bytes.push_back(std::byte{static_cast<std::uint8_t>((index * 3U) & 0xFFU)});
    }
    return bytes;
}

inline void append_frame(std::vector<std::byte>& bytes, const FrameSpec& frame)
{
    append_i32_le(bytes, frame.origin_x);
    append_i32_le(bytes, frame.origin_y);
    append_i32_le(bytes, frame.width);
    append_i32_le(bytes, frame.height);
    bytes.insert(bytes.end(), frame.pixels.begin(), frame.pixels.end());
}

inline void append_single_entry(
    std::vector<std::byte>& bytes,
    const FrameSpec& frame = {})
{
    append_i32_le(bytes, 0);
    append_frame(bytes, frame);
}

inline void append_group_entry(
    std::vector<std::byte>& bytes,
    const std::span<const float> cumulative_intervals,
    const std::span<const FrameSpec> frames)
{
    append_i32_le(bytes, 1);
    append_i32_le(bytes, static_cast<std::int32_t>(frames.size()));
    for (const auto interval : cumulative_intervals) {
        append_f32_le(bytes, interval);
    }
    for (const auto& frame : frames) {
        append_frame(bytes, frame);
    }
}

[[nodiscard]] inline std::vector<std::byte> literal_single_sprite(
    const std::int32_t texture_format = 0,
    const std::int32_t orientation = 2,
    const std::int32_t sync_type = 0)
{
    auto bytes = literal_header_and_palette(
        1, texture_format, orientation, sync_type);
    append_single_entry(bytes, FrameSpec{-1, 2, 2, 2,
        {std::byte{0}, std::byte{1}, std::byte{0xFF}, std::byte{2}}});
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> literal_group_sprite()
{
    auto bytes = literal_header_and_palette(1, 0, 4, 1, 2, 2);
    const std::vector<float> intervals{0.10F, 0.35F};
    const std::vector<FrameSpec> frames{
        FrameSpec{-1, 1, 2, 1, {std::byte{3}, std::byte{4}}},
        FrameSpec{0, 2, 1, 2, {std::byte{5}, std::byte{6}}},
    };
    append_group_entry(bytes, intervals, frames);
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> literal_two_single_sprite()
{
    auto bytes = literal_header_and_palette(2);
    append_single_entry(bytes,
        FrameSpec{-1, 0, 1, 1, {std::byte{7}}});
    append_single_entry(bytes,
        FrameSpec{1, 0, 1, 1, {std::byte{8}}});
    return bytes;
}

} // namespace hlclient::tests::sprite_fixture

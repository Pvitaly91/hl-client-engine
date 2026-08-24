#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::tests {

inline constexpr std::size_t kSyntheticWad3HeaderSize = 12U;
inline constexpr std::size_t kSyntheticWad3DirectoryEntrySize = 32U;
inline constexpr std::uint8_t kSyntheticWad3MiptexType = 0x43U;

inline void synthetic_wad3_append_u8(
    std::vector<std::byte>& bytes,
    const std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

inline void synthetic_wad3_append_u16le(
    std::vector<std::byte>& bytes,
    const std::uint16_t value)
{
    synthetic_wad3_append_u8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    synthetic_wad3_append_u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

inline void synthetic_wad3_append_u32le(
    std::vector<std::byte>& bytes,
    const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index) {
        synthetic_wad3_append_u8(
            bytes,
            static_cast<std::uint8_t>(value >> static_cast<unsigned int>(index * 8U)));
    }
}

inline void synthetic_wad3_append_i32le(
    std::vector<std::byte>& bytes,
    const std::int32_t value)
{
    synthetic_wad3_append_u32le(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void synthetic_wad3_write_u8(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint8_t value)
{
    bytes[offset] = static_cast<std::byte>(value);
}

inline void synthetic_wad3_write_u32le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(
                value >> static_cast<unsigned int>(index * 8U)));
    }
}

inline void synthetic_wad3_write_i32le(
    const std::span<std::byte> bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    synthetic_wad3_write_u32le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] inline std::uint32_t synthetic_wad3_read_u32le(
    const std::span<const std::byte> bytes,
    const std::size_t offset)
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return value;
}

[[nodiscard]] inline std::size_t synthetic_wad3_directory_offset(
    const std::span<const std::byte> bytes)
{
    return static_cast<std::size_t>(synthetic_wad3_read_u32le(bytes, 8U));
}

[[nodiscard]] inline std::size_t synthetic_wad3_directory_entry_offset(
    const std::span<const std::byte> bytes,
    const std::size_t ordinal)
{
    return synthetic_wad3_directory_offset(bytes) +
        ordinal * kSyntheticWad3DirectoryEntrySize;
}

[[nodiscard]] inline std::vector<std::byte> synthetic_goldsrc_miptex(
    const std::string_view name = "WAD_TEXTURE",
    const std::uint32_t width = 16U,
    const std::uint32_t height = 16U,
    const std::uint8_t level_zero_seed = 0U)
{
    std::vector<std::byte> bytes;
    bytes.resize(40U, std::byte{0});
    const auto copy_count = std::min<std::size_t>(name.size(), 16U);
    for (std::size_t index = 0U; index < copy_count; ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<std::uint8_t>(name[index]));
    }
    synthetic_wad3_write_u32le(bytes, 16U, width);
    synthetic_wad3_write_u32le(bytes, 20U, height);

    std::size_t next_offset = 40U;
    for (std::size_t level = 0U; level < 4U; ++level) {
        synthetic_wad3_write_u32le(
            bytes,
            24U + level * 4U,
            static_cast<std::uint32_t>(next_offset));
        const auto level_width = static_cast<std::size_t>(width >> level);
        const auto level_height = static_cast<std::size_t>(height >> level);
        const auto pixel_count = level_width * level_height;
        for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
            synthetic_wad3_append_u8(
                bytes,
                static_cast<std::uint8_t>(level_zero_seed + pixel + level));
        }
        next_offset += pixel_count;
    }

    synthetic_wad3_append_u16le(bytes, 256U);
    for (std::size_t index = 0U; index < 256U; ++index) {
        synthetic_wad3_append_u8(bytes, static_cast<std::uint8_t>(index));
        synthetic_wad3_append_u8(bytes, static_cast<std::uint8_t>(255U - index));
        synthetic_wad3_append_u8(bytes, static_cast<std::uint8_t>(index ^ 0x5AU));
    }
    return bytes;
}

struct SyntheticWad3Entry {
    std::string_view name{"WAD_TEXTURE"};
    std::vector<std::byte> payload{synthetic_goldsrc_miptex()};
    std::uint8_t type{kSyntheticWad3MiptexType};
    std::uint8_t compression{0U};
    std::uint8_t padding0{0U};
    std::uint8_t padding1{0U};
    std::optional<std::int32_t> disk_size;
    std::optional<std::int32_t> uncompressed_size;
};

struct SyntheticWad3Fixture {
    std::vector<std::byte> bytes;
    std::vector<std::size_t> payload_offsets;
    std::size_t directory_offset{0U};
};

[[nodiscard]] inline SyntheticWad3Fixture synthetic_wad3(
    const std::span<const SyntheticWad3Entry> entries)
{
    SyntheticWad3Fixture fixture;
    fixture.bytes.reserve(kSyntheticWad3HeaderSize);
    synthetic_wad3_append_u8(fixture.bytes, 'W');
    synthetic_wad3_append_u8(fixture.bytes, 'A');
    synthetic_wad3_append_u8(fixture.bytes, 'D');
    synthetic_wad3_append_u8(fixture.bytes, '3');
    synthetic_wad3_append_i32le(
        fixture.bytes, static_cast<std::int32_t>(entries.size()));
    synthetic_wad3_append_i32le(fixture.bytes, 0);

    fixture.payload_offsets.reserve(entries.size());
    for (const auto& entry : entries) {
        fixture.payload_offsets.push_back(fixture.bytes.size());
        fixture.bytes.insert(
            fixture.bytes.end(), entry.payload.begin(), entry.payload.end());
    }
    fixture.directory_offset = fixture.bytes.size();
    synthetic_wad3_write_i32le(
        fixture.bytes, 8U, static_cast<std::int32_t>(fixture.directory_offset));

    for (std::size_t ordinal = 0U; ordinal < entries.size(); ++ordinal) {
        const auto& entry = entries[ordinal];
        const auto payload_size = static_cast<std::int32_t>(entry.payload.size());
        synthetic_wad3_append_i32le(
            fixture.bytes, static_cast<std::int32_t>(fixture.payload_offsets[ordinal]));
        synthetic_wad3_append_i32le(
            fixture.bytes, entry.disk_size.value_or(payload_size));
        synthetic_wad3_append_i32le(
            fixture.bytes, entry.uncompressed_size.value_or(payload_size));
        synthetic_wad3_append_u8(fixture.bytes, entry.type);
        synthetic_wad3_append_u8(fixture.bytes, entry.compression);
        synthetic_wad3_append_u8(fixture.bytes, entry.padding0);
        synthetic_wad3_append_u8(fixture.bytes, entry.padding1);
        const auto copy_count = std::min<std::size_t>(entry.name.size(), 16U);
        for (std::size_t index = 0U; index < copy_count; ++index) {
            synthetic_wad3_append_u8(
                fixture.bytes,
                static_cast<std::uint8_t>(entry.name[index]));
        }
        for (std::size_t index = copy_count; index < 16U; ++index) {
            synthetic_wad3_append_u8(fixture.bytes, 0U);
        }
    }
    return fixture;
}

[[nodiscard]] inline SyntheticWad3Fixture synthetic_wad3(
    std::initializer_list<SyntheticWad3Entry> entries)
{
    return synthetic_wad3(std::span<const SyntheticWad3Entry>{entries.begin(), entries.size()});
}

[[nodiscard]] inline SyntheticWad3Fixture synthetic_valid_wad3(
    const std::string_view name = "WAD_TEXTURE")
{
    auto entry = SyntheticWad3Entry{};
    entry.name = name;
    entry.payload = synthetic_goldsrc_miptex(name);
    return synthetic_wad3({std::move(entry)});
}

} // namespace hlclient::tests

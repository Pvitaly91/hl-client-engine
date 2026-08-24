#pragma once

#include <cstddef>
#include <cstdint>

namespace hlclient::goldsrc::wad3 {

inline constexpr std::size_t kGoldSrcWad3HeaderWireSize = 12U;
inline constexpr std::size_t kGoldSrcWad3DirectoryEntryWireSize = 32U;
inline constexpr std::size_t kGoldSrcWad3EntryNameWireSize = 16U;

// Pinned Valve tool evidence: wadlib.h defines TYP_LUMPY as 64, qlumpy.c
// places `miptex` at commands[3], and emits TYP_LUMPY + command ordinal.
inline constexpr std::uint8_t kGoldSrcWad3MiptexType = 0x43U;
inline constexpr std::uint8_t kGoldSrcWad3NoCompression = 0U;

inline constexpr std::size_t kGoldSrcWad3DefaultMaximumSourceBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcWad3HardMaximumSourceBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t kGoldSrcWad3DefaultMaximumLumpCount = 4'096U;
inline constexpr std::size_t kGoldSrcWad3HardMaximumLumpCount = 65'536U;

} // namespace hlclient::goldsrc::wad3

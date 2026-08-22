#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::test::user_info_fixture {

// Independently authored, sanitized literal. No production builder produces
// this fixture. Layout: opcode 13, zero-based u8 client index, private u32le
// user ID, ordered byte-preserving info string plus NUL, and 16 synthetic
// opaque bytes.
inline constexpr std::array kExactUserInfoMessage{
    std::byte{0x0dU},
    std::byte{0x02U},
    std::byte{0x78U}, std::byte{0x56U}, std::byte{0x34U}, std::byte{0x12U},
    std::byte{'\\'},
    std::byte{'b'}, std::byte{'o'}, std::byte{'t'}, std::byte{'t'},
    std::byte{'o'}, std::byte{'m'}, std::byte{'c'}, std::byte{'o'},
    std::byte{'l'}, std::byte{'o'}, std::byte{'r'},
    std::byte{'\\'}, std::byte{'6'},
    std::byte{'\\'},
    std::byte{'m'}, std::byte{'o'}, std::byte{'d'}, std::byte{'e'},
    std::byte{'l'},
    std::byte{'\\'},
    std::byte{'s'}, std::byte{'c'}, std::byte{'i'}, std::byte{'e'},
    std::byte{'n'}, std::byte{'t'}, std::byte{'i'}, std::byte{'s'},
    std::byte{'t'},
    std::byte{'\\'},
    std::byte{'t'}, std::byte{'o'}, std::byte{'p'}, std::byte{'c'},
    std::byte{'o'}, std::byte{'l'}, std::byte{'o'}, std::byte{'r'},
    std::byte{'\\'}, std::byte{'3'}, std::byte{'0'},
    std::byte{'\\'},
    std::byte{'n'}, std::byte{'a'}, std::byte{'m'}, std::byte{'e'},
    std::byte{'\\'},
    std::byte{'S'}, std::byte{'y'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'h'}, std::byte{'e'}, std::byte{'t'}, std::byte{'i'},
    std::byte{'c'},
    std::byte{0x00U},
    std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U}, std::byte{0xa4U},
    std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U}, std::byte{0xa8U},
    std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU}, std::byte{0xacU},
    std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU}, std::byte{0xb0U},
};

static_assert(kExactUserInfoMessage.size() == 80U);
inline constexpr std::size_t kInfoStringOffset = 6U;
inline constexpr std::size_t kInfoStringLength = 57U;
inline constexpr std::size_t kInfoTerminatorOffset =
    kInfoStringOffset + kInfoStringLength;
inline constexpr std::size_t kOpaqueSuffixOffset =
    kInfoTerminatorOffset + 1U;

inline constexpr std::array kSyntheticOpaqueSuffix{
    std::byte{0xc1U}, std::byte{0xc2U}, std::byte{0xc3U}, std::byte{0xc4U},
    std::byte{0xc5U}, std::byte{0xc6U}, std::byte{0xc7U}, std::byte{0xc8U},
    std::byte{0xc9U}, std::byte{0xcaU}, std::byte{0xcbU}, std::byte{0xccU},
    std::byte{0xcdU}, std::byte{0xceU}, std::byte{0xcfU}, std::byte{0xd0U},
};

inline void append_u32_le(
    std::vector<std::byte>& bytes,
    const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

[[nodiscard]] inline std::vector<std::byte> make_message(
    const std::uint8_t client_index,
    const std::uint32_t private_user_id_wire,
    const std::string_view private_info_string,
    const std::span<const std::byte, 16U> opaque_suffix =
        kSyntheticOpaqueSuffix)
{
    std::vector<std::byte> bytes;
    bytes.reserve(23U + private_info_string.size());
    bytes.push_back(std::byte{13U});
    bytes.push_back(static_cast<std::byte>(client_index));
    append_u32_le(bytes, private_user_id_wire);
    const auto info_bytes = std::as_bytes(std::span{
        private_info_string.data(),
        private_info_string.size(),
    });
    bytes.insert(bytes.end(), info_bytes.begin(), info_bytes.end());
    bytes.push_back(std::byte{0U});
    bytes.insert(bytes.end(), opaque_suffix.begin(), opaque_suffix.end());
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> exact_message()
{
    return {kExactUserInfoMessage.begin(), kExactUserInfoMessage.end()};
}

} // namespace hlclient::test::user_info_fixture

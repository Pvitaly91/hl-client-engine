#pragma once

#include <array>
#include <cstddef>

namespace hlclient::test::resource_client_response_fixture {

inline constexpr std::array<std::byte, 16U> kSyntheticOpaqueMaterial{
    std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U},
    std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U},
    std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU},
    std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU},
};

// Independently authored literal. Production code never generates the expected
// bytes used by codec tests. SHA-256:
// 77AF845BE1360A3C3E0D92E129D0E05D36C4F0C826B118A0B56196D7041BD154
inline constexpr std::array<std::byte, 41U> kExactSyntheticResponse{
    std::byte{0x05U},
    std::byte{0x01U}, std::byte{0x00U},
    std::byte{'s'}, std::byte{'y'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'h'}, std::byte{'e'}, std::byte{'t'}, std::byte{'i'},
    std::byte{'c'}, std::byte{'.'}, std::byte{'w'}, std::byte{'a'},
    std::byte{'d'}, std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0x04U},
    std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U},
    std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U},
    std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU},
    std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU},
};

} // namespace hlclient::test::resource_client_response_fixture

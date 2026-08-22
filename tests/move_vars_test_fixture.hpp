#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace hlclient::test::move_vars_fixture {

// Independent sanitized literal. It is not produced by a production codec.
// Layout: opcode 44, 16 f32le, one footsteps byte, 8 f32le, "desert" + NUL.
inline constexpr std::array kExactMoveVarsMessage{
    std::byte{0x2cU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x48U}, std::byte{0x44U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc8U}, std::byte{0x42U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xa0U}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xfaU}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x41U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x41U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x20U}, std::byte{0x41U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3fU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x90U}, std::byte{0x41U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0xfaU}, std::byte{0x44U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x45U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x01U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x48U}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x43U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U}, std::byte{0x3eU},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0xbfU},
    std::byte{'d'}, std::byte{'e'}, std::byte{'s'}, std::byte{'e'},
    std::byte{'r'}, std::byte{'t'}, std::byte{0x00U},
};

static_assert(kExactMoveVarsMessage.size() == 105U);

// Independent literal continuation: opcode 32, opcode 5, two opcode-39
// definitions (signed sizes -1 and 9), two opcode-9 strings, then the exact
// neutral opcode-13 boundary with one untouched body byte.
inline constexpr std::array kExactPostMoveVarsStream{
    std::byte{32U}, std::byte{0U}, std::byte{0U},
    std::byte{5U}, std::byte{1U}, std::byte{0U},

    std::byte{39U}, std::byte{64U}, std::byte{0xffU},
    std::byte{'H'}, std::byte{'u'}, std::byte{'d'}, std::byte{'T'},
    std::byte{'e'}, std::byte{'x'}, std::byte{'t'}, std::byte{0U},
    std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},

    std::byte{39U}, std::byte{65U}, std::byte{9U},
    std::byte{'S'}, std::byte{'c'}, std::byte{'o'}, std::byte{'r'},
    std::byte{'e'}, std::byte{'I'}, std::byte{'n'}, std::byte{'f'},
    std::byte{'o'}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},

    std::byte{9U},
    std::byte{'s'}, std::byte{'y'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'h'}, std::byte{'e'}, std::byte{'t'}, std::byte{'i'},
    std::byte{'c'}, std::byte{' '}, std::byte{'c'}, std::byte{'o'},
    std::byte{'m'}, std::byte{'m'}, std::byte{'a'}, std::byte{'n'},
    std::byte{'d'}, std::byte{' '}, std::byte{'o'}, std::byte{'n'},
    std::byte{'e'}, std::byte{0U},

    std::byte{9U},
    std::byte{'s'}, std::byte{'y'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'h'}, std::byte{'e'}, std::byte{'t'}, std::byte{'i'},
    std::byte{'c'}, std::byte{' '}, std::byte{'c'}, std::byte{'o'},
    std::byte{'m'}, std::byte{'m'}, std::byte{'a'}, std::byte{'n'},
    std::byte{'d'}, std::byte{' '}, std::byte{'t'}, std::byte{'w'},
    std::byte{'o'}, std::byte{0U},

    std::byte{13U}, std::byte{0xa5U},
};

static_assert(kExactPostMoveVarsStream.size() == 92U);

struct Values {
    float gravity{800.0F};
    float stop_speed{100.0F};
    float maximum_speed{320.0F};
    float spectator_maximum_speed{500.0F};
    float acceleration{10.0F};
    float air_acceleration{10.0F};
    float water_acceleration{10.0F};
    float friction{4.0F};
    float edge_friction{2.0F};
    float water_friction{1.0F};
    float entity_gravity{1.0F};
    float bounce{1.0F};
    float step_size{18.0F};
    float maximum_velocity{2'000.0F};
    float z_maximum{4'096.0F};
    float wave_height{0.0F};
    std::uint8_t footsteps{1U};
    float roll_angle{2.0F};
    float roll_speed{200.0F};
    float sky_color_red{128.0F};
    float sky_color_green{128.0F};
    float sky_color_blue{128.0F};
    float sky_vector_x{0.25F};
    float sky_vector_y{0.0F};
    float sky_vector_z{-0.5F};
    std::string_view sky_name{"desert"};
};

inline void append_u16_le(
    std::vector<std::byte>& bytes,
    const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

inline void append_u32_le(
    std::vector<std::byte>& bytes,
    const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

inline void append_float32_le(
    std::vector<std::byte>& bytes,
    const float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    append_u32_le(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void append_string(
    std::vector<std::byte>& bytes,
    const std::string_view value)
{
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.push_back(std::byte{0U});
}

inline void append_move_vars_body(
    std::vector<std::byte>& bytes,
    const Values& values = {})
{
    const std::array prefix{
        values.gravity,
        values.stop_speed,
        values.maximum_speed,
        values.spectator_maximum_speed,
        values.acceleration,
        values.air_acceleration,
        values.water_acceleration,
        values.friction,
        values.edge_friction,
        values.water_friction,
        values.entity_gravity,
        values.bounce,
        values.step_size,
        values.maximum_velocity,
        values.z_maximum,
        values.wave_height,
    };
    for (const auto value : prefix) {
        append_float32_le(bytes, value);
    }
    bytes.push_back(static_cast<std::byte>(values.footsteps));
    const std::array tail{
        values.roll_angle,
        values.roll_speed,
        values.sky_color_red,
        values.sky_color_green,
        values.sky_color_blue,
        values.sky_vector_x,
        values.sky_vector_y,
        values.sky_vector_z,
    };
    for (const auto value : tail) {
        append_float32_le(bytes, value);
    }
    append_string(bytes, values.sky_name);
}

inline void append_move_vars_message(
    std::vector<std::byte>& bytes,
    const Values& values = {})
{
    bytes.push_back(std::byte{44U});
    append_move_vars_body(bytes, values);
}

[[nodiscard]] inline std::vector<std::byte> move_vars_message(
    const Values& values = {})
{
    std::vector<std::byte> bytes;
    append_move_vars_message(bytes, values);
    return bytes;
}

inline void append_opcode_32_control(
    std::vector<std::byte>& bytes,
    const std::uint8_t first = 0U,
    const std::uint8_t second = 0U)
{
    bytes.push_back(std::byte{32U});
    bytes.push_back(static_cast<std::byte>(first));
    bytes.push_back(static_cast<std::byte>(second));
}

inline void append_opcode_5_control(
    std::vector<std::byte>& bytes,
    const std::uint16_t value = 1U)
{
    bytes.push_back(std::byte{5U});
    append_u16_le(bytes, value);
}

inline void append_opcode_39_control(
    std::vector<std::byte>& bytes,
    const std::uint8_t identifier,
    const std::int8_t declared_size,
    const std::string_view name)
{
    if (name.empty() || name.size() >= 16U) {
        throw std::invalid_argument{"Synthetic user-message name must fit 15 bytes"};
    }
    bytes.push_back(std::byte{39U});
    bytes.push_back(static_cast<std::byte>(identifier));
    bytes.push_back(static_cast<std::byte>(
        std::bit_cast<std::uint8_t>(declared_size)));
    const auto source = std::as_bytes(std::span{name.data(), name.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.insert(bytes.end(), 16U - name.size(), std::byte{0U});
}

inline void append_opcode_9_control(
    std::vector<std::byte>& bytes,
    const std::string_view value)
{
    bytes.push_back(std::byte{9U});
    append_string(bytes, value);
}

inline void append_confirmed_controls(std::vector<std::byte>& bytes)
{
    append_opcode_32_control(bytes);
    append_opcode_5_control(bytes);
    append_opcode_39_control(bytes, 64U, std::int8_t{-1}, "HudText");
    append_opcode_39_control(bytes, 65U, std::int8_t{9}, "ScoreInfo");
    append_opcode_9_control(bytes, "synthetic command one");
    append_opcode_9_control(bytes, "synthetic command two");
}

inline void append_opcode_13_boundary(
    std::vector<std::byte>& bytes,
    const std::span<const std::byte> opaque_body = {})
{
    bytes.push_back(std::byte{13U});
    if (opaque_body.empty()) {
        bytes.push_back(std::byte{0xa5U});
    } else {
        bytes.insert(bytes.end(), opaque_body.begin(), opaque_body.end());
    }
}

[[nodiscard]] inline std::vector<std::byte> move_vars_body_and_post_stream(
    const Values& values = {},
    const bool include_controls = true)
{
    std::vector<std::byte> bytes;
    append_move_vars_body(bytes, values);
    if (include_controls) {
        append_confirmed_controls(bytes);
    }
    append_opcode_13_boundary(bytes);
    return bytes;
}

} // namespace hlclient::test::move_vars_fixture

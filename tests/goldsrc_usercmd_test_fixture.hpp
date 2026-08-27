#pragma once

#include "delta_test_fixture.hpp"

#include <hlclient/goldsrc/usercmd_delta_codec.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>
#include <hlclient/goldsrc/usercmd_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace hlclient::test::usercmd_fixture {

namespace goldsrc = hlclient::goldsrc;
namespace delta_fixture = hlclient::test::delta_fixture;

// Independently stated test descriptor. Production binding/factory code is not
// used to create it, so schema validation cannot pass by construction.
inline constexpr std::array<delta_fixture::Field, 15U> kExactFields{{
    {"lerp_msec", 0x0000'0002U, 0U, 9U, 4'000U, 4'000U},
    {"msec", 0x0000'0001U, 2U, 8U, 4'000U, 4'000U},
    {"viewangles[1]", 0x0000'0010U, 8U, 16U, 4'000U, 4'000U},
    {"viewangles[0]", 0x0000'0010U, 4U, 16U, 4'000U, 4'000U},
    {"buttons", 0x0000'0002U, 30U, 16U, 4'000U, 4'000U},
    {"forwardmove", 0x8000'0004U, 16U, 12U, 4'000U, 4'000U},
    {"lightlevel", 0x0000'0001U, 28U, 8U, 4'000U, 4'000U},
    {"sidemove", 0x8000'0004U, 20U, 12U, 4'000U, 4'000U},
    {"upmove", 0x8000'0004U, 24U, 12U, 4'000U, 4'000U},
    {"impulse", 0x0000'0001U, 32U, 8U, 4'000U, 4'000U},
    {"viewangles[2]", 0x0000'0010U, 12U, 16U, 4'000U, 4'000U},
    {"impact_index", 0x0000'0008U, 36U, 6U, 4'000U, 4'000U},
    {"impact_position[0]", 0x8000'0004U, 40U, 16U, 32'000U, 4'000U},
    {"impact_position[1]", 0x8000'0004U, 44U, 16U, 32'000U, 4'000U},
    {"impact_position[2]", 0x8000'0004U, 48U, 16U, 32'000U, 4'000U},
}};

inline constexpr std::array<goldsrc::GoldSrcUserCmdSemanticField, 15U>
    kExactSemantics{{
        goldsrc::GoldSrcUserCmdSemanticField::lerp_msec,
        goldsrc::GoldSrcUserCmdSemanticField::msec,
        goldsrc::GoldSrcUserCmdSemanticField::view_yaw,
        goldsrc::GoldSrcUserCmdSemanticField::view_pitch,
        goldsrc::GoldSrcUserCmdSemanticField::buttons,
        goldsrc::GoldSrcUserCmdSemanticField::forward_move,
        goldsrc::GoldSrcUserCmdSemanticField::light_level,
        goldsrc::GoldSrcUserCmdSemanticField::side_move,
        goldsrc::GoldSrcUserCmdSemanticField::up_move,
        goldsrc::GoldSrcUserCmdSemanticField::impulse,
        goldsrc::GoldSrcUserCmdSemanticField::view_roll,
        goldsrc::GoldSrcUserCmdSemanticField::impact_index,
        goldsrc::GoldSrcUserCmdSemanticField::impact_position_x,
        goldsrc::GoldSrcUserCmdSemanticField::impact_position_y,
        goldsrc::GoldSrcUserCmdSemanticField::impact_position_z,
    }};

inline constexpr std::array<std::byte, 1U> kZeroDelta{
    std::byte{0x00U}};

inline constexpr std::array<std::byte, 4U> kFieldZero257Delta{
    std::byte{0x01U}, std::byte{0x01U},
    std::byte{0x01U}, std::byte{0x01U}};

inline constexpr std::array<std::byte, 5U> kHighestFieldMinusOneDelta{
    std::byte{0x02U}, std::byte{0x00U}, std::byte{0x40U},
    std::byte{0xf8U}, std::byte{0xffU}};

// Independently packed: two mask bytes ff/7f, then all 15 values in exact
// field order. There are 211 meaningful bits and five zero padding bits.
inline constexpr std::array<std::byte, 27U> kAllFieldsDelta{
    std::byte{0x02U}, std::byte{0xffU}, std::byte{0x7fU},
    std::byte{0x2cU}, std::byte{0x1fU}, std::byte{0x00U},
    std::byte{0x80U}, std::byte{0x00U}, std::byte{0x80U},
    std::byte{0x69U}, std::byte{0x24U}, std::byte{0x80U},
    std::byte{0x02U}, std::byte{0x08U}, std::byte{0xecU},
    std::byte{0x15U}, std::byte{0xe0U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x30U}, std::byte{0x52U},
    std::byte{0x00U}, std::byte{0x60U}, std::byte{0xffU},
    std::byte{0xc7U}, std::byte{0x00U}, std::byte{0x00U},
};

[[nodiscard]] inline goldsrc::DeltaSchemaRegistryState registry_from_fields(
    const std::span<const delta_fixture::Field> fields,
    const std::string_view schema_name = "usercmd_t")
{
    const auto bytes = delta_fixture::schema(schema_name, fields);
    const auto parsed = goldsrc::DeltaDescriptionParser{}.parse(bytes, 0U);
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::move(builder).publish();
}

[[nodiscard]] inline goldsrc::DeltaSchemaRegistryState exact_registry()
{
    return registry_from_fields(kExactFields);
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdSchemaBinding exact_binding()
{
    auto registry = exact_registry();
    auto result = goldsrc::bind_goldsrc_usercmd_schema(registry);
    REQUIRE(result);
    REQUIRE(result.binding);
    return std::move(*result.binding);
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdSequence sequence(
    const std::uint32_t value)
{
    const auto result = goldsrc::GoldSrcUserCmdSequence::create(value);
    REQUIRE(result);
    return *result;
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdState state(
    const goldsrc::GoldSrcUserCmdCreateInfo& info,
    const goldsrc::GoldSrcUserCmdLimits& limits = {})
{
    auto result = goldsrc::GoldSrcUserCmdState::create(info, limits);
    REQUIRE(result);
    REQUIRE(result.state);
    return std::move(*result.state);
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdState default_state(
    const std::uint32_t sequence_value)
{
    return state(goldsrc::goldsrc_usercmd_default_create_info(
        sequence(sequence_value)));
}

[[nodiscard]] inline goldsrc::GoldSrcUserCmdState full_state(
    const std::uint32_t sequence_value)
{
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        sequence(sequence_value));
    info.lerp_msec = 300U;
    info.msec = 15U;
    info.view_angles = {270.0F, 90.0F, 180.0F};
    info.forward_move = 320.0F;
    info.side_move = -160.0F;
    info.up_move = 10.0F;
    info.light_level = 64U;
    info.buttons = 0x1234U;
    info.impulse = 7U;
    info.impact_index = 17;
    info.impact_position = {1.25F, -2.5F, 3.0F};
    info.sample_duration_nanoseconds = 15'000'000U;
    return state(info);
}

[[nodiscard]] inline std::shared_ptr<const goldsrc::GoldSrcUserCmdState>
shared_state(goldsrc::GoldSrcUserCmdState value)
{
    return std::make_shared<const goldsrc::GoldSrcUserCmdState>(
        std::move(value));
}

} // namespace hlclient::test::usercmd_fixture

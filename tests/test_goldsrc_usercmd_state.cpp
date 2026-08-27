#include <hlclient/goldsrc/usercmd_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence command_sequence(
    const std::uint32_t value)
{
    const auto sequence = goldsrc::GoldSrcUserCmdSequence::create(value);
    REQUIRE(sequence);
    return *sequence;
}

[[nodiscard]] goldsrc::GoldSrcUserCmdCreateInfo valid_create_info(
    const std::uint32_t sequence = 1U)
{
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        command_sequence(sequence), -12'345);
    info.lerp_msec = 17U;
    info.msec = 16U;
    info.view_angles = {12.5F, -25.0F, 5.0F};
    info.forward_move = 320.0F;
    info.side_move = -64.0F;
    info.up_move = 8.0F;
    info.light_level = 123U;
    info.buttons = 0x0123U;
    info.impulse = 7U;
    info.weapon_select = 0U;
    info.impact_index = 3;
    info.impact_position = {1.0F, -2.0F, 3.0F};
    info.source_input_sequence = 99U;
    info.sample_duration_nanoseconds = 16'000'000U;
    return info;
}

void require_error(
    const goldsrc::GoldSrcUserCmdState::CreationResult& result,
    const goldsrc::GoldSrcUserCmdErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

} // namespace

TEST_CASE("GoldSrc usercmd state is an immutable owning synthetic value",
          "[goldsrc][usercmd][state][immutable]")
{
    static_assert(!std::is_default_constructible_v<goldsrc::GoldSrcUserCmdState>);
    static_assert(std::is_copy_constructible_v<goldsrc::GoldSrcUserCmdState>);
    static_assert(std::is_nothrow_move_constructible_v<
                  goldsrc::GoldSrcUserCmdState>);
    static_assert(!std::is_copy_assignable_v<goldsrc::GoldSrcUserCmdState>);
    static_assert(!std::is_move_assignable_v<goldsrc::GoldSrcUserCmdState>);

    auto info = valid_create_info();
    const auto expected_angles = info.view_angles;
    const auto expected_impact = info.impact_position;
    auto created = goldsrc::GoldSrcUserCmdState::create(info);
    REQUIRE(created);
    REQUIRE(created.state);

    info.view_angles[0U] = 300.0F;
    info.impact_position[0U] = 100.0F;
    info.buttons = 0U;

    const auto& state = *created.state;
    CHECK(state.lerp_msec() == 17U);
    CHECK(state.msec() == 16U);
    CHECK(state.view_angles() == expected_angles);
    CHECK(state.forward_move() == 320.0F);
    CHECK(state.side_move() == -64.0F);
    CHECK(state.up_move() == 8.0F);
    CHECK(state.light_level() == 123U);
    CHECK(state.buttons() == 0x0123U);
    CHECK(state.impulse() == 7U);
    CHECK(state.weapon_select() == 0U);
    CHECK(state.impact_index() == 3);
    CHECK(state.impact_position() == expected_impact);
    CHECK(state.command_sequence() == command_sequence(1U));
    CHECK(state.compatibility_profile() ==
          goldsrc::GoldSrcUserCmdCompatibilityProfile::synthetic_usercmd_v1);
    CHECK(state.input_mapping_profile() ==
          goldsrc::GoldSrcUserCmdInputMappingProfile::synthetic_explicit_v1);
    CHECK(state.schema_binding_profile() ==
          goldsrc::GoldSrcUserCmdSchemaBindingProfile::
              synthetic_usercmd_schema_v1);
    CHECK(state.source_input_sequence() == 99U);
    CHECK(state.sample_time_nanoseconds() == -12'345);
    CHECK(state.sample_duration_nanoseconds() == 16'000'000U);
}

TEST_CASE("GoldSrc usercmd sequence and safety limits are explicitly bounded",
          "[goldsrc][usercmd][state][limits]")
{
    CHECK_FALSE(goldsrc::GoldSrcUserCmdSequence{}.valid());
    CHECK_FALSE(goldsrc::GoldSrcUserCmdSequence::create(0U));
    CHECK_FALSE(goldsrc::GoldSrcUserCmdSequence::create(3U, 2U));
    REQUIRE(goldsrc::GoldSrcUserCmdSequence::create(2U, 2U));
    CHECK(goldsrc::valid_goldsrc_usercmd_limits({}));
    CHECK(goldsrc::valid_goldsrc_usercmd_limits(
        goldsrc::kGoldSrcUserCmdHardLimits));

    auto invalid = goldsrc::GoldSrcUserCmdLimits{};
    invalid.maximum_msec = 0U;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_limits(invalid));
    require_error(
        goldsrc::GoldSrcUserCmdState::create(valid_create_info(), invalid),
        goldsrc::GoldSrcUserCmdErrorCode::invalid_limits);

    invalid = {};
    invalid.maximum_angle_magnitude = 359.0F;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_limits(invalid));
    invalid = {};
    invalid.maximum_move_magnitude =
        std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_limits(invalid));
    invalid = {};
    invalid.maximum_commands_per_packet = 2U;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_limits(invalid));
    invalid = {};
    invalid.maximum_encoded_bits = invalid.maximum_encoded_bytes * 8U + 1U;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_limits(invalid));

    auto limited = goldsrc::GoldSrcUserCmdLimits{};
    limited.maximum_command_sequence = 2U;
    require_error(
        goldsrc::GoldSrcUserCmdState::create(valid_create_info(3U), limited),
        goldsrc::GoldSrcUserCmdErrorCode::invalid_sequence);
}

TEST_CASE("GoldSrc usercmd state gates all stock and mismatched profiles",
          "[goldsrc][usercmd][state][profile][stock-pending]")
{
    auto stock = valid_create_info();
    stock.compatibility_profile =
        goldsrc::GoldSrcUserCmdCompatibilityProfile::
            stock_protocol_48_build_10210;
    stock.input_mapping_profile =
        goldsrc::GoldSrcUserCmdInputMappingProfile::
            stock_protocol_48_controlled_profile_v1;
    stock.schema_binding_profile =
        goldsrc::GoldSrcUserCmdSchemaBindingProfile::
            stock_protocol_48_build_10210_schema_only;
    require_error(
        goldsrc::GoldSrcUserCmdState::create(stock),
        goldsrc::GoldSrcUserCmdErrorCode::stock_evidence_pending);

    auto pending = valid_create_info();
    pending.compatibility_profile =
        goldsrc::GoldSrcUserCmdCompatibilityProfile::
            stock_protocol_48_evidence_pending;
    require_error(
        goldsrc::GoldSrcUserCmdState::create(pending),
        goldsrc::GoldSrcUserCmdErrorCode::stock_evidence_pending);

    auto controlled_input = valid_create_info();
    controlled_input.input_mapping_profile =
        goldsrc::GoldSrcUserCmdInputMappingProfile::
            stock_protocol_48_controlled_profile_v1;
    require_error(
        goldsrc::GoldSrcUserCmdState::create(controlled_input),
        goldsrc::GoldSrcUserCmdErrorCode::stock_evidence_pending);

    auto mismatched = valid_create_info();
    mismatched.schema_binding_profile =
        goldsrc::GoldSrcUserCmdSchemaBindingProfile::
            stock_protocol_48_build_10210_schema_only;
    require_error(
        goldsrc::GoldSrcUserCmdState::create(mismatched),
        goldsrc::GoldSrcUserCmdErrorCode::invalid_profile);

    auto unknown = valid_create_info();
    unknown.compatibility_profile =
        static_cast<goldsrc::GoldSrcUserCmdCompatibilityProfile>(0xffU);
    require_error(
        goldsrc::GoldSrcUserCmdState::create(unknown),
        goldsrc::GoldSrcUserCmdErrorCode::invalid_profile);
}

TEST_CASE("GoldSrc usercmd state accepts exact bounds and rejects invalid numerics",
          "[goldsrc][usercmd][state][bounds][finite]")
{
    SECTION("exact configured bounds are admitted")
    {
        auto boundary = valid_create_info();
        boundary.lerp_msec = 511U;
        boundary.msec = 255U;
        boundary.sample_duration_nanoseconds = 255'000'000U;
        boundary.view_angles = {360.0F, -360.0F, 0.0F};
        boundary.forward_move = 2'047.0F;
        boundary.side_move = -2'047.0F;
        boundary.up_move = 2'047.0F;
        boundary.buttons = UINT16_MAX;
        boundary.impact_index = 63;
        boundary.impact_position = {4'095.875F, -4'095.875F, 0.125F};
        REQUIRE(goldsrc::GoldSrcUserCmdState::create(boundary));
    }

    SECTION("non-finite values are rejected before magnitude checks")
    {
        auto invalid = valid_create_info();
        invalid.view_angles[1U] =
            std::numeric_limits<float>::quiet_NaN();
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::non_finite_value);
        invalid = valid_create_info();
        invalid.forward_move = std::numeric_limits<float>::infinity();
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::non_finite_value);
    }

    SECTION("duration and lerp bounds are independent")
    {
        auto invalid = valid_create_info();
        invalid.lerp_msec = 512U;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::lerp_out_of_range);
        invalid = valid_create_info();
        invalid.sample_duration_nanoseconds = 256'000'000U;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::duration_out_of_range);
    }

    SECTION("movement angle and button bounds are typed")
    {
        auto invalid = valid_create_info();
        invalid.side_move = -2'048.0F;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::movement_out_of_range);
        invalid = valid_create_info();
        invalid.view_angles[2U] = 361.0F;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::angle_out_of_range);
        invalid = valid_create_info();
        auto limits = goldsrc::GoldSrcUserCmdLimits{};
        limits.maximum_buttons_mask = 0x0003U;
        invalid.buttons = 0x0004U;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid, limits),
            goldsrc::GoldSrcUserCmdErrorCode::buttons_out_of_range);
    }

    SECTION("impact fields are bounded and internally consistent")
    {
        auto invalid = valid_create_info();
        invalid.impact_index = 64;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::impact_out_of_range);
        invalid = valid_create_info();
        invalid.impact_position[0U] = 4'096.0F;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::impact_out_of_range);
        invalid = valid_create_info();
        invalid.impact_index = 0;
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::impossible_impact_fields);
        invalid = valid_create_info();
        invalid.impact_position = {};
        require_error(
            goldsrc::GoldSrcUserCmdState::create(invalid),
            goldsrc::GoldSrcUserCmdErrorCode::impossible_impact_fields);
    }
}

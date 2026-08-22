#include "move_vars_test_fixture.hpp"

#include <hlclient/goldsrc/move_vars.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fixture = hlclient::test::move_vars_fixture;
namespace goldsrc = hlclient::goldsrc;

inline constexpr std::size_t kGravityOffset = 1U;
inline constexpr std::size_t kFootstepsOffset = 1U + 16U * 4U;
inline constexpr std::size_t kTailOffset = kFootstepsOffset + 1U;
inline constexpr std::size_t kSkyNameOffset = kTailOffset + 8U * 4U;

void overwrite_u32_le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    REQUIRE(offset + 4U <= bytes.size());
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void overwrite_float32_le(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const float value)
{
    overwrite_u32_le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void check_error(
    const goldsrc::MoveVarsParseResult& result,
    const goldsrc::MoveVarsErrorCode expected,
    const std::size_t input_size)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error.has_value());
    CHECK(result.error->code == expected);
    CHECK(result.error->byte_offset <= input_size);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kMoveVarsDiagnosticTextLimit);
    CHECK_FALSE(result.state.has_value());
    CHECK(result.bytes_consumed == 0U);
    CHECK(result.next_byte_offset == 0U);
}

void check_exact_state(const goldsrc::MoveVarsState& state)
{
    CHECK(state.gravity() == 800.0F);
    CHECK(state.stop_speed() == 100.0F);
    CHECK(state.maximum_speed() == 320.0F);
    CHECK(state.spectator_maximum_speed() == 500.0F);
    CHECK(state.acceleration() == 10.0F);
    CHECK(state.air_acceleration() == 10.0F);
    CHECK(state.water_acceleration() == 10.0F);
    CHECK(state.friction() == 4.0F);
    CHECK(state.edge_friction() == 2.0F);
    CHECK(state.water_friction() == 1.0F);
    CHECK(state.entity_gravity() == 1.0F);
    CHECK(state.bounce() == 1.0F);
    CHECK(state.step_size() == 18.0F);
    CHECK(state.maximum_velocity() == 2'000.0F);
    CHECK(state.z_maximum() == 4'096.0F);
    CHECK(state.wave_height() == 0.0F);
    CHECK(state.footsteps());
    CHECK(state.roll_angle() == 2.0F);
    CHECK(state.roll_speed() == 200.0F);
    CHECK(state.sky_color_red() == 128.0F);
    CHECK(state.sky_color_green() == 128.0F);
    CHECK(state.sky_color_blue() == 128.0F);
    CHECK(state.sky_vector_x() == 0.25F);
    CHECK(state.sky_vector_y() == 0.0F);
    CHECK(state.sky_vector_z() == -0.5F);
    CHECK(state.sky_name() == "desert");
    CHECK(state.source_message_offset() == 0U);
    CHECK(state.body_bytes() == 104U);
    CHECK(state.message_bytes() == fixture::kExactMoveVarsMessage.size());
    CHECK(
        state.compatibility_profile() ==
        goldsrc::MoveVarsCompatibilityProfile::
            stock_protocol_48_build_10210);
    CHECK(
        state.evidence_profile() ==
        goldsrc::MoveVarsEvidenceProfile::
            stock_capture_and_public_valve_header);
}

[[nodiscard]] goldsrc::MoveVarsState parse_values(
    const fixture::Values& values)
{
    const auto result = goldsrc::MoveVarsParser{}.parse(
        fixture::move_vars_message(values),
        0U);
    REQUIRE(result);
    REQUIRE(result.state);
    return std::move(*result.state);
}

struct MoveVarsSnapshot {
    std::array<std::uint32_t, goldsrc::kMoveVarsNumericFieldCount>
        numeric_bits{};
    bool footsteps{false};
    std::string sky_name;
};

[[nodiscard]] MoveVarsSnapshot snapshot(const goldsrc::MoveVarsState& state)
{
    return {
        {
            std::bit_cast<std::uint32_t>(state.gravity()),
            std::bit_cast<std::uint32_t>(state.stop_speed()),
            std::bit_cast<std::uint32_t>(state.maximum_speed()),
            std::bit_cast<std::uint32_t>(state.spectator_maximum_speed()),
            std::bit_cast<std::uint32_t>(state.acceleration()),
            std::bit_cast<std::uint32_t>(state.air_acceleration()),
            std::bit_cast<std::uint32_t>(state.water_acceleration()),
            std::bit_cast<std::uint32_t>(state.friction()),
            std::bit_cast<std::uint32_t>(state.edge_friction()),
            std::bit_cast<std::uint32_t>(state.water_friction()),
            std::bit_cast<std::uint32_t>(state.entity_gravity()),
            std::bit_cast<std::uint32_t>(state.bounce()),
            std::bit_cast<std::uint32_t>(state.step_size()),
            std::bit_cast<std::uint32_t>(state.maximum_velocity()),
            std::bit_cast<std::uint32_t>(state.z_maximum()),
            std::bit_cast<std::uint32_t>(state.wave_height()),
            std::bit_cast<std::uint32_t>(state.roll_angle()),
            std::bit_cast<std::uint32_t>(state.roll_speed()),
            std::bit_cast<std::uint32_t>(state.sky_color_red()),
            std::bit_cast<std::uint32_t>(state.sky_color_green()),
            std::bit_cast<std::uint32_t>(state.sky_color_blue()),
            std::bit_cast<std::uint32_t>(state.sky_vector_x()),
            std::bit_cast<std::uint32_t>(state.sky_vector_y()),
            std::bit_cast<std::uint32_t>(state.sky_vector_z()),
        },
        state.footsteps(),
        state.sky_name(),
    };
}

void check_only_numeric_member_changed(
    const MoveVarsSnapshot& candidate,
    const MoveVarsSnapshot& baseline,
    const std::size_t changed_index,
    const float expected_value)
{
    REQUIRE(changed_index < candidate.numeric_bits.size());
    REQUIRE(candidate.numeric_bits.size() == baseline.numeric_bits.size());
    const auto expected_bits = std::bit_cast<std::uint32_t>(expected_value);
    for (std::size_t index = 0U;
         index < candidate.numeric_bits.size();
         ++index) {
        INFO("numeric getter index " << index);
        if (index == changed_index) {
            CHECK(candidate.numeric_bits[index] == expected_bits);
            CHECK(candidate.numeric_bits[index] != baseline.numeric_bits[index]);
        } else {
            CHECK(candidate.numeric_bits[index] == baseline.numeric_bits[index]);
        }
    }
    CHECK(candidate.footsteps == baseline.footsteps);
    CHECK(candidate.sky_name == baseline.sky_name);
}

void check_only_footsteps_changed(
    const MoveVarsSnapshot& candidate,
    const MoveVarsSnapshot& baseline,
    const bool expected_value)
{
    CHECK(candidate.numeric_bits == baseline.numeric_bits);
    CHECK(candidate.footsteps == expected_value);
    CHECK(candidate.footsteps != baseline.footsteps);
    CHECK(candidate.sky_name == baseline.sky_name);
}

void check_only_sky_name_changed(
    const MoveVarsSnapshot& candidate,
    const MoveVarsSnapshot& baseline,
    const std::string_view expected_value)
{
    CHECK(candidate.numeric_bits == baseline.numeric_bits);
    CHECK(candidate.footsteps == baseline.footsteps);
    CHECK(candidate.sky_name == expected_value);
    CHECK(candidate.sky_name != baseline.sky_name);
}

TEST_CASE("Move-vars parser decodes the exact sanitized literal transactionally",
          "[goldsrc][movevars][fixture]")
{
    const auto generated = fixture::move_vars_message();
    REQUIRE(generated.size() == fixture::kExactMoveVarsMessage.size());
    CHECK(std::ranges::equal(generated, fixture::kExactMoveVarsMessage));

    const auto result = goldsrc::MoveVarsParser{}.parse(
        fixture::kExactMoveVarsMessage,
        0U);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK_FALSE(result.error);
    CHECK(result.bytes_consumed == fixture::kExactMoveVarsMessage.size());
    CHECK(result.next_byte_offset == fixture::kExactMoveVarsMessage.size());
    check_exact_state(*result.state);

    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<goldsrc::MoveVarsState>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<goldsrc::MoveVarsState>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<goldsrc::MoveVarsState>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<goldsrc::MoveVarsState>);
    STATIC_REQUIRE(std::is_same_v<
                   decltype(std::declval<const goldsrc::MoveVarsState&>().sky_name()),
                   const std::string&>);
}

TEST_CASE("Every truncated move-vars message prefix fails without publication",
          "[goldsrc][movevars][truncation]")
{
    const goldsrc::MoveVarsParser parser;
    for (std::size_t size = 0U;
         size < fixture::kExactMoveVarsMessage.size();
         ++size) {
        INFO("prefix size " << size);
        const auto result = parser.parse(
            std::span{fixture::kExactMoveVarsMessage}.first(size),
            0U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->byte_offset <= size);
        CHECK_FALSE(result.state);
        CHECK(result.bytes_consumed == 0U);
        CHECK(result.next_byte_offset == 0U);
    }
}

TEST_CASE("Move-vars parser requires the exact opcode and input cursor",
          "[goldsrc][movevars][cursor][opcode]")
{
    auto wrong = fixture::move_vars_message();
    wrong[0U] = std::byte{43U};
    check_error(
        goldsrc::MoveVarsParser{}.parse(wrong, 0U),
        goldsrc::MoveVarsErrorCode::wrong_opcode,
        wrong.size());

    const std::array outside{std::byte{44U}};
    check_error(
        goldsrc::MoveVarsParser{}.parse(outside, outside.size()),
        goldsrc::MoveVarsErrorCode::invalid_input_geometry,
        outside.size());

    std::vector<std::byte> prefixed{std::byte{0xa5U}, std::byte{0x5aU}};
    prefixed.insert(
        prefixed.end(),
        fixture::kExactMoveVarsMessage.begin(),
        fixture::kExactMoveVarsMessage.end());
    prefixed.push_back(std::byte{13U});
    const auto offset_result = goldsrc::MoveVarsParser{}.parse(prefixed, 2U);
    REQUIRE(offset_result);
    REQUIRE(offset_result.state);
    CHECK(offset_result.state->source_message_offset() == 2U);
    CHECK(offset_result.bytes_consumed == fixture::kExactMoveVarsMessage.size());
    CHECK(offset_result.next_byte_offset == 2U + fixture::kExactMoveVarsMessage.size());
    CHECK(prefixed[offset_result.next_byte_offset] == std::byte{13U});
}

TEST_CASE("Move-vars float32 decoding fails closed for hostile IEEE encodings",
          "[goldsrc][movevars][float][security]")
{
    const goldsrc::MoveVarsParser parser;
    for (const auto bits : std::array{
             0x7fc0'0000U,
             0x7f80'0000U,
             0xff80'0000U,
         }) {
        auto bytes = fixture::move_vars_message();
        overwrite_u32_le(bytes, kGravityOffset, bits);
        check_error(
            parser.parse(bytes, 0U),
            goldsrc::MoveVarsErrorCode::non_finite_numeric_field,
            bytes.size());
    }

    auto too_large = fixture::move_vars_message();
    overwrite_float32_le(
        too_large,
        kGravityOffset,
        goldsrc::kMaximumMoveVarsNumericMagnitude + 1.0F);
    check_error(
        parser.parse(too_large, 0U),
        goldsrc::MoveVarsErrorCode::numeric_magnitude_exceeded,
        too_large.size());

    auto wrong_endianness = fixture::move_vars_message();
    overwrite_u32_le(wrong_endianness, kGravityOffset, 0x3f80'004aU);
    const auto native_order = parser.parse(wrong_endianness, 0U);
    REQUIRE(native_order);
    REQUIRE(native_order.state);
    CHECK(std::bit_cast<std::uint32_t>(native_order.state->gravity()) ==
          0x3f80'004aU);
    std::reverse(
        wrong_endianness.begin() + static_cast<std::ptrdiff_t>(kGravityOffset),
        wrong_endianness.begin() +
            static_cast<std::ptrdiff_t>(kGravityOffset + 4U));
    check_error(
        parser.parse(wrong_endianness, 0U),
        goldsrc::MoveVarsErrorCode::numeric_magnitude_exceeded,
        wrong_endianness.size());
}

TEST_CASE("Move-vars preserves positive and negative subnormal encodings exactly",
          "[goldsrc][movevars][float][subnormal]")
{
    for (const auto bits :
         std::array<std::uint32_t, 2U>{0x0000'0001U, 0x8000'0001U}) {
        auto bytes = fixture::move_vars_message();
        overwrite_u32_le(bytes, kGravityOffset, bits);
        const auto result = goldsrc::MoveVarsParser{}.parse(bytes, 0U);
        REQUIRE(result);
        REQUIRE(result.state);
        CHECK(std::bit_cast<std::uint32_t>(result.state->gravity()) == bits);
        CHECK(std::signbit(result.state->gravity()) ==
              ((bits & 0x8000'0000U) != 0U));
    }
}

TEST_CASE("Move-vars accepts and preserves signed zero while rejecting invalid boolean bytes",
          "[goldsrc][movevars][float][boolean]")
{
    auto negative_zero = fixture::move_vars_message();
    overwrite_u32_le(negative_zero, kTailOffset + 6U * 4U, 0x8000'0000U);
    const auto accepted = goldsrc::MoveVarsParser{}.parse(negative_zero, 0U);
    REQUIRE(accepted);
    REQUIRE(accepted.state);
    CHECK(accepted.state->sky_vector_y() == 0.0F);
    CHECK(std::signbit(accepted.state->sky_vector_y()));

    for (const auto invalid : std::array<std::uint8_t, 3U>{2U, 127U, 255U}) {
        auto bytes = fixture::move_vars_message();
        bytes[kFootstepsOffset] = static_cast<std::byte>(invalid);
        check_error(
            goldsrc::MoveVarsParser{}.parse(bytes, 0U),
            goldsrc::MoveVarsErrorCode::invalid_footsteps,
            bytes.size());
    }

    auto disabled_values = fixture::Values{};
    disabled_values.footsteps = 0U;
    const auto disabled = parse_values(disabled_values);
    CHECK_FALSE(disabled.footsteps());
}

TEST_CASE("Move-vars sky string is bounded, NUL-terminated, owning, and unnormalized",
          "[goldsrc][movevars][string][limits][ownership]")
{
    goldsrc::MoveVarsLimits limits;
    limits.maximum_sky_name_length = 64U;
    const goldsrc::MoveVarsParser parser{limits};

    std::string exact_limit(64U, 's');
    auto exact_values = fixture::Values{};
    exact_values.sky_name = exact_limit;
    const auto exact = parser.parse(fixture::move_vars_message(exact_values), 0U);
    REQUIRE(exact);
    REQUIRE(exact.state);
    CHECK(exact.state->sky_name() == exact_limit);

    std::string over_limit(65U, 's');
    auto over_values = fixture::Values{};
    over_values.sky_name = over_limit;
    const auto over_bytes = fixture::move_vars_message(over_values);
    check_error(
        parser.parse(over_bytes, 0U),
        goldsrc::MoveVarsErrorCode::sky_name_too_long,
        over_bytes.size());

    auto unterminated = fixture::move_vars_message();
    unterminated.pop_back();
    check_error(
        parser.parse(unterminated, 0U),
        goldsrc::MoveVarsErrorCode::unterminated_sky_name,
        unterminated.size());

    std::optional<goldsrc::MoveVarsParseResult> owning;
    {
        auto bytes = fixture::move_vars_message();
        owning.emplace(parser.parse(bytes, 0U));
        std::ranges::fill(bytes, std::byte{0U});
    }
    REQUIRE(*owning);
    REQUIRE(owning->state);
    CHECK(owning->state->sky_name() == "desert");

    auto raw_name = fixture::move_vars_message();
    raw_name[kSkyNameOffset] = std::byte{0x1bU};
    const auto raw = parser.parse(raw_name, 0U);
    REQUIRE(raw);
    REQUIRE(raw.state);
    REQUIRE(raw.state->sky_name().size() == 6U);
    CHECK(static_cast<unsigned char>(raw.state->sky_name()[0U]) == 0x1bU);
}

TEST_CASE("Move-vars parser leaves following service bytes exactly unconsumed",
          "[goldsrc][movevars][cursor][no-scan]")
{
    auto bytes = fixture::move_vars_message();
    bytes.push_back(std::byte{99U});
    bytes.push_back(std::byte{44U});
    bytes.push_back(std::byte{13U});
    const auto result = goldsrc::MoveVarsParser{}.parse(bytes, 0U);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.bytes_consumed == fixture::kExactMoveVarsMessage.size());
    CHECK(result.next_byte_offset == fixture::kExactMoveVarsMessage.size());
    CHECK(bytes[result.next_byte_offset] == std::byte{99U});
}

TEST_CASE("Move-vars differential variants change only their typed getter",
          "[goldsrc][movevars][differential]")
{
    const auto baseline = snapshot(parse_values({}));

    auto gravity_values = fixture::Values{};
    gravity_values.gravity = 400.0F;
    check_only_numeric_member_changed(
        snapshot(parse_values(gravity_values)),
        baseline,
        0U,
        400.0F);

    auto speed_values = fixture::Values{};
    speed_values.maximum_speed = 640.0F;
    check_only_numeric_member_changed(
        snapshot(parse_values(speed_values)),
        baseline,
        2U,
        640.0F);

    auto acceleration_values = fixture::Values{};
    acceleration_values.acceleration = 15.0F;
    check_only_numeric_member_changed(
        snapshot(parse_values(acceleration_values)),
        baseline,
        4U,
        15.0F);

    auto friction_values = fixture::Values{};
    friction_values.friction = 6.0F;
    check_only_numeric_member_changed(
        snapshot(parse_values(friction_values)),
        baseline,
        7U,
        6.0F);

    auto step_values = fixture::Values{};
    step_values.step_size = 24.0F;
    check_only_numeric_member_changed(
        snapshot(parse_values(step_values)),
        baseline,
        12U,
        24.0F);

    auto footsteps_values = fixture::Values{};
    footsteps_values.footsteps = 0U;
    check_only_footsteps_changed(
        snapshot(parse_values(footsteps_values)),
        baseline,
        false);

    auto sky_values = fixture::Values{};
    sky_values.sky_name = "synthetic_sky";
    check_only_sky_name_changed(
        snapshot(parse_values(sky_values)),
        baseline,
        "synthetic_sky");
}

TEST_CASE("Move-vars limit configuration is positive and hard capped",
          "[goldsrc][movevars][configuration]")
{
    CHECK(goldsrc::valid_move_vars_limits({}));
    CHECK(goldsrc::valid_move_vars_limits({
        goldsrc::kMaximumMoveVarsStringLength,
        goldsrc::kMaximumPostMoveVarsStringLength,
        goldsrc::kMaximumPostMoveVarsControls,
    }));
    CHECK_FALSE(goldsrc::valid_move_vars_limits({
        0U,
        goldsrc::kDefaultMaximumPostMoveVarsStringLength,
        goldsrc::kDefaultMaximumPostMoveVarsControls,
    }));
    CHECK_FALSE(goldsrc::valid_move_vars_limits({
        goldsrc::kMaximumMoveVarsStringLength + 1U,
        goldsrc::kDefaultMaximumPostMoveVarsStringLength,
        goldsrc::kDefaultMaximumPostMoveVarsControls,
    }));
    CHECK_FALSE(goldsrc::valid_move_vars_limits({
        goldsrc::kDefaultMaximumMoveVarsStringLength,
        0U,
        goldsrc::kDefaultMaximumPostMoveVarsControls,
    }));
    CHECK_FALSE(goldsrc::valid_move_vars_limits({
        goldsrc::kDefaultMaximumMoveVarsStringLength,
        goldsrc::kDefaultMaximumPostMoveVarsStringLength,
        0U,
    }));

    const goldsrc::MoveVarsParser invalid{{0U, 1U, 1U}};
    check_error(
        invalid.parse(fixture::kExactMoveVarsMessage, 0U),
        goldsrc::MoveVarsErrorCode::invalid_configuration,
        fixture::kExactMoveVarsMessage.size());

    const goldsrc::MoveVarsParser unsupported_profile{
        {},
        static_cast<goldsrc::MoveVarsCompatibilityProfile>(0xffU),
    };
    CHECK_FALSE(unsupported_profile.valid_configuration());
    check_error(
        unsupported_profile.parse(fixture::kExactMoveVarsMessage, 0U),
        goldsrc::MoveVarsErrorCode::invalid_configuration,
        fixture::kExactMoveVarsMessage.size());
}

} // namespace

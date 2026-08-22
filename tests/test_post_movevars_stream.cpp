#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"

#include <hlclient/goldsrc/move_vars.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace goldsrc = hlclient::goldsrc;

struct DeltaInput {
    goldsrc::OwnedServicePayload payload;
    goldsrc::DeltaDescriptionStreamState delta;
};

[[nodiscard]] DeltaInput decode_delta(
    std::vector<std::byte> post_delta_body,
    const std::uint8_t boundary_opcode = 44U)
{
    const std::vector<std::vector<std::byte>> schemas{
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
    };
    auto pre_resource = delta_fixture::decode_pre_resource(
        delta_fixture::service_payload(
            schemas,
            boundary_opcode,
            post_delta_body));
    const auto decoded = goldsrc::DeltaDescriptionStreamDecoder{}.decode(
        pre_resource.payload.bytes,
        pre_resource.state.boundary());
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    return DeltaInput{
        std::move(pre_resource.payload),
        std::move(*decoded.state),
    };
}

[[nodiscard]] std::vector<std::byte> full_post_delta_body()
{
    std::vector<std::byte> body;
    body.insert(
        body.end(),
        move_fixture::kExactMoveVarsMessage.begin() + 1,
        move_fixture::kExactMoveVarsMessage.end());
    body.insert(
        body.end(),
        move_fixture::kExactPostMoveVarsStream.begin(),
        move_fixture::kExactPostMoveVarsStream.end());
    return body;
}

void check_stream_error(
    const goldsrc::MoveVarsStreamDecodeResult& result,
    const goldsrc::MoveVarsStreamErrorCode expected)
{
    INFO("expected " << goldsrc::to_string(expected));
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK_FALSE(result.error->context.empty());
    CHECK(result.error->context.size() <= goldsrc::kMoveVarsDiagnosticTextLimit);
    CHECK_FALSE(result.state);
    CHECK(result.required_event_count == 0U);
}

TEST_CASE("Post-movevars stream publishes exact confirmed controls and neutral boundary",
          "[goldsrc][movevars][stream][fixture]")
{
    auto input = decode_delta(full_post_delta_body());
    const auto initial_offset = input.delta.boundary.byte_offset();
    const auto decoded = goldsrc::MoveVarsStreamDecoder{}.decode(
        input.payload.bytes,
        input.delta.boundary);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    CHECK_FALSE(decoded.error);
    CHECK(decoded.required_event_count == 8U);
    CHECK(decoded.state->control_count() == 6U);
    CHECK(decoded.state->move_vars().gravity() == 800.0F);
    CHECK(decoded.state->move_vars().sky_name() == "desert");
    CHECK(decoded.state->bytes_consumed() ==
          decoded.state->boundary().byte_offset() - initial_offset);
    CHECK(input.payload.bytes[
              initial_offset + move_fixture::kExactMoveVarsMessage.size()] ==
          std::byte{32U});

    const auto& controls = decoded.state->controls();
    REQUIRE(controls.size() == 6U);
    CHECK(controls[0U].opcode() == 32U);
    CHECK(controls[0U].kind() ==
          goldsrc::PostMoveVarsControlKind::opcode_32_two_byte);
    REQUIRE(std::holds_alternative<goldsrc::PostMoveVarsOpcode32Control>(
        controls[0U].body()));
    const auto& opcode32 =
        std::get<goldsrc::PostMoveVarsOpcode32Control>(controls[0U].body());
    CHECK(opcode32.first_value == 0U);
    CHECK(opcode32.second_value == 0U);
    CHECK(controls[0U].byte_count() == 3U);

    CHECK(controls[1U].kind() ==
          goldsrc::PostMoveVarsControlKind::opcode_5_uint16_le);
    CHECK(std::get<goldsrc::PostMoveVarsOpcode5Control>(controls[1U].body()).value == 1U);
    CHECK(controls[1U].byte_count() == 3U);

    const auto& definition =
        std::get<goldsrc::PostMoveVarsUserMessageDefinition>(controls[2U].body());
    CHECK(definition.identifier == 64U);
    CHECK(definition.declared_size == std::int8_t{-1});
    CHECK(definition.name == "HudText");
    CHECK(controls[2U].byte_count() == 19U);

    const auto& variable_definition =
        std::get<goldsrc::PostMoveVarsUserMessageDefinition>(controls[3U].body());
    CHECK(variable_definition.declared_size == std::int8_t{9});
    CHECK(variable_definition.name == "ScoreInfo");

    CHECK(std::get<goldsrc::PostMoveVarsStringControl>(controls[4U].body()).value ==
          "synthetic command one");
    CHECK(std::get<goldsrc::PostMoveVarsStringControl>(controls[5U].body()).value ==
          "synthetic command two");

    const auto& boundary = decoded.state->boundary();
    CHECK(boundary.opcode() == goldsrc::kStockPostMoveVarsBoundaryOpcode);
    CHECK(boundary.category() ==
          goldsrc::PostMoveVarsBoundaryCategory::stock_observed_opcode_13);
    CHECK(boundary.evidence_status() ==
          goldsrc::PostMoveVarsBoundaryEvidenceStatus::
              stock_confirmed_opcode_13_body_unconsumed);
    CHECK(boundary.remaining_byte_count() == 1U);
    CHECK(boundary.byte_offset() ==
          initial_offset + move_fixture::kExactMoveVarsMessage.size() +
              move_fixture::kExactPostMoveVarsStream.size() - 2U);
    CHECK(input.payload.bytes[boundary.byte_offset()] == std::byte{13U});
    CHECK(input.payload.bytes[boundary.byte_offset() + 1U] == std::byte{0xa5U});
}

TEST_CASE("Post-movevars stream supports zero, one, and multiple confirmed controls",
          "[goldsrc][movevars][stream][controls]")
{
    SECTION("zero controls") {
        auto input = decode_delta(
            move_fixture::move_vars_body_and_post_stream({}, false));
        const auto decoded = goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary);
        REQUIRE(decoded);
        REQUIRE(decoded.state);
        CHECK(decoded.state->controls().empty());
        CHECK(decoded.required_event_count == 2U);
    }

    SECTION("one control") {
        std::vector<std::byte> body;
        move_fixture::append_move_vars_body(body);
        move_fixture::append_opcode_5_control(body, 0x1234U);
        move_fixture::append_opcode_13_boundary(body);
        auto input = decode_delta(std::move(body));
        const auto decoded = goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary);
        REQUIRE(decoded);
        REQUIRE(decoded.state);
        REQUIRE(decoded.state->controls().size() == 1U);
        CHECK(std::get<goldsrc::PostMoveVarsOpcode5Control>(
                  decoded.state->controls()[0U].body()).value == 0x1234U);
        CHECK(decoded.required_event_count == 3U);
    }

    SECTION("multiple controls preserve wire order") {
        auto input = decode_delta(full_post_delta_body());
        const auto decoded = goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary);
        REQUIRE(decoded);
        REQUIRE(decoded.state);
        const std::array expected{32U, 5U, 39U, 39U, 9U, 9U};
        REQUIRE(decoded.state->controls().size() == expected.size());
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            CHECK(decoded.state->controls()[index].opcode() == expected[index]);
        }
    }
}

TEST_CASE("Post-movevars boundary opcode and body remain entirely unconsumed",
          "[goldsrc][movevars][stream][boundary][no-resource-parse]")
{
    const std::array opaque_body{
        std::byte{0x11U},
        std::byte{0x2bU},
        std::byte{0x44U},
        std::byte{0x00U},
    };
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    move_fixture::append_opcode_13_boundary(body, opaque_body);
    auto input = decode_delta(std::move(body));
    const auto decoded = goldsrc::MoveVarsStreamDecoder{}.decode(
        input.payload.bytes,
        input.delta.boundary);
    REQUIRE(decoded);
    REQUIRE(decoded.state);
    const auto& boundary = decoded.state->boundary();
    CHECK(boundary.remaining_byte_count() == opaque_body.size());
    CHECK(input.payload.bytes[boundary.byte_offset()] == std::byte{13U});
    CHECK(std::ranges::equal(
        std::span{input.payload.bytes}.subspan(
            boundary.byte_offset() + 1U),
        opaque_body));
    CHECK(decoded.state->bytes_consumed() ==
          boundary.byte_offset() - input.delta.boundary.byte_offset());
}

TEST_CASE("Opcode 44 as the final message is a typed missing-boundary failure",
          "[goldsrc][movevars][stream][boundary][missing]")
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    auto input = decode_delta(std::move(body));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::missing_post_movevars_boundary);
}

TEST_CASE("Post-movevars stream rejects a bodyless opcode-13 boundary",
          "[goldsrc][movevars][stream][boundary][malformed]")
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    body.push_back(std::byte{13U});
    auto input = decode_delta(std::move(body));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::malformed_post_movevars_boundary);
}

TEST_CASE("Post-movevars stream never scans past an unknown opcode",
          "[goldsrc][movevars][stream][unsupported][no-scan]")
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    body.push_back(std::byte{99U});
    body.push_back(std::byte{44U});
    body.push_back(std::byte{13U});
    body.push_back(std::byte{0xa5U});
    auto input = decode_delta(std::move(body));
    const auto result = goldsrc::MoveVarsStreamDecoder{}.decode(
        input.payload.bytes,
        input.delta.boundary);
    check_stream_error(
        result,
        goldsrc::MoveVarsStreamErrorCode::unsupported_post_movevars_opcode);
    REQUIRE(result.error);
    CHECK(result.error->wire_opcode == 99U);
}

TEST_CASE("A direct opcode-43 candidate is not mislabeled without the response-gated transition",
          "[goldsrc][movevars][stream][resource][neutral]")
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    body.push_back(std::byte{43U});
    body.push_back(std::byte{0xa5U});
    auto input = decode_delta(std::move(body));
    const auto result = goldsrc::MoveVarsStreamDecoder{}.decode(
        input.payload.bytes,
        input.delta.boundary);
    check_stream_error(
        result,
        goldsrc::MoveVarsStreamErrorCode::unsupported_post_movevars_opcode);
    REQUIRE(result.error);
    CHECK(result.error->wire_opcode == 43U);
}

TEST_CASE("Post-movevars stream rejects duplicate opcode 44 transactionally",
          "[goldsrc][movevars][stream][duplicate]")
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    move_fixture::append_move_vars_message(body);
    move_fixture::append_opcode_13_boundary(body);
    auto input = decode_delta(std::move(body));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::duplicate_move_vars);
}

TEST_CASE("Every confirmed post-movevars control fails safely when truncated",
          "[goldsrc][movevars][stream][control][truncation]")
{
    const std::array truncated_controls{
        std::vector<std::byte>{std::byte{32U}},
        std::vector<std::byte>{std::byte{32U}, std::byte{0U}},
        std::vector<std::byte>{std::byte{5U}},
        std::vector<std::byte>{std::byte{5U}, std::byte{1U}},
        std::vector<std::byte>{std::byte{39U}},
        std::vector<std::byte>{std::byte{39U}, std::byte{64U}, std::byte{8U}},
    };
    for (const auto& truncated : truncated_controls) {
        std::vector<std::byte> body;
        move_fixture::append_move_vars_body(body);
        body.insert(body.end(), truncated.begin(), truncated.end());
        auto input = decode_delta(std::move(body));
        check_stream_error(
            goldsrc::MoveVarsStreamDecoder{}.decode(
                input.payload.bytes,
                input.delta.boundary),
            goldsrc::MoveVarsStreamErrorCode::truncated_control);
    }

    std::vector<std::byte> unterminated;
    move_fixture::append_move_vars_body(unterminated);
    unterminated.push_back(std::byte{9U});
    unterminated.push_back(std::byte{'x'});
    auto input = decode_delta(std::move(unterminated));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::unterminated_control_string);
}

TEST_CASE("Opcode-39 fixed names require NUL termination and zero padding",
          "[goldsrc][movevars][stream][control][reserved]")
{
    std::vector<std::byte> valid;
    move_fixture::append_move_vars_body(valid);
    const auto control_offset = valid.size();
    move_fixture::append_opcode_39_control(
        valid,
        64U,
        std::int8_t{8},
        "VoiceMask");
    move_fixture::append_opcode_13_boundary(valid);

    auto no_terminator = valid;
    std::fill_n(
        no_terminator.begin() +
            static_cast<std::ptrdiff_t>(control_offset + 3U),
        16U,
        std::byte{'X'});
    auto no_terminator_input = decode_delta(std::move(no_terminator));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            no_terminator_input.payload.bytes,
            no_terminator_input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_control_value);

    auto bad_padding = valid;
    bad_padding[control_offset + 3U + 15U] = std::byte{1U};
    auto bad_padding_input = decode_delta(std::move(bad_padding));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            bad_padding_input.payload.bytes,
            bad_padding_input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_control_value);
}

TEST_CASE("Opcode-9 strings and total controls obey project hard bounds",
          "[goldsrc][movevars][stream][limits]")
{
    goldsrc::MoveVarsLimits string_limits;
    string_limits.maximum_control_string_length = 4U;
    std::vector<std::byte> long_string;
    move_fixture::append_move_vars_body(long_string);
    move_fixture::append_opcode_9_control(long_string, "12345");
    move_fixture::append_opcode_13_boundary(long_string);
    auto string_input = decode_delta(std::move(long_string));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{string_limits}.decode(
            string_input.payload.bytes,
            string_input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::control_string_too_long);

    goldsrc::MoveVarsLimits control_limits;
    control_limits.maximum_post_movevars_controls = 1U;
    std::vector<std::byte> two_controls;
    move_fixture::append_move_vars_body(two_controls);
    move_fixture::append_opcode_32_control(two_controls);
    move_fixture::append_opcode_5_control(two_controls);
    move_fixture::append_opcode_13_boundary(two_controls);
    auto control_input = decode_delta(std::move(two_controls));
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{control_limits}.decode(
            control_input.payload.bytes,
            control_input.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::control_limit_exceeded);
}

TEST_CASE("Move-vars parser failure is forwarded without partial stream state",
          "[goldsrc][movevars][stream][parser][transaction]")
{
    auto body = full_post_delta_body();
    REQUIRE(body.size() >= 4U);
    body[0U] = std::byte{0x00U};
    body[1U] = std::byte{0x00U};
    body[2U] = std::byte{0xc0U};
    body[3U] = std::byte{0x7fU};
    auto input = decode_delta(std::move(body));
    const auto result = goldsrc::MoveVarsStreamDecoder{}.decode(
        input.payload.bytes,
        input.delta.boundary);
    check_stream_error(
        result,
        goldsrc::MoveVarsStreamErrorCode::move_vars_parse_failed);
    REQUIRE(result.error);
    CHECK(result.error->parser_code ==
          goldsrc::MoveVarsErrorCode::non_finite_numeric_field);
}

TEST_CASE("Move-vars continuation validates the owning post-delta geometry",
          "[goldsrc][movevars][stream][geometry]")
{
    auto wrong_opcode = decode_delta(
        std::vector<std::byte>{std::byte{0xa5U}},
        99U);
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            wrong_opcode.payload.bytes,
            wrong_opcode.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::wrong_initial_opcode);

    auto changed_payload = decode_delta(full_post_delta_body());
    changed_payload.payload.bytes.push_back(std::byte{0U});
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            changed_payload.payload.bytes,
            changed_payload.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_boundary_geometry);

    auto stale_boundary = decode_delta(full_post_delta_body());
    const auto stale_offset = stale_boundary.delta.boundary.byte_offset();
    const auto shortened_payload = std::span<const std::byte>{
        stale_boundary.payload.bytes}.first(stale_offset);
    check_stream_error(
        goldsrc::MoveVarsStreamDecoder{}.decode(
            shortened_payload,
            stale_boundary.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_boundary_geometry);

    const goldsrc::MoveVarsStreamDecoder invalid{{0U, 1U, 1U}};
    auto normal = decode_delta(full_post_delta_body());
    check_stream_error(
        invalid.decode(normal.payload.bytes, normal.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_configuration);

    const goldsrc::MoveVarsStreamDecoder unsupported_profile{
        {},
        static_cast<goldsrc::MoveVarsCompatibilityProfile>(0xffU),
    };
    CHECK_FALSE(unsupported_profile.valid_configuration());
    check_stream_error(
        unsupported_profile.decode(
            normal.payload.bytes,
            normal.delta.boundary),
        goldsrc::MoveVarsStreamErrorCode::invalid_configuration);
}

TEST_CASE("Post-movevars state owns all strings after source payload destruction",
          "[goldsrc][movevars][stream][ownership]")
{
    std::optional<goldsrc::MoveVarsStreamDecodeResult> result;
    {
        auto input = decode_delta(full_post_delta_body());
        result.emplace(goldsrc::MoveVarsStreamDecoder{}.decode(
            input.payload.bytes,
            input.delta.boundary));
        std::ranges::fill(input.payload.bytes, std::byte{0U});
    }
    REQUIRE(*result);
    REQUIRE(result->state);
    CHECK(result->state->move_vars().sky_name() == "desert");
    const auto& definition =
        std::get<goldsrc::PostMoveVarsUserMessageDefinition>(
            result->state->controls()[0U + 2U].body());
    CHECK(definition.name == "HudText");
    CHECK(std::get<goldsrc::PostMoveVarsStringControl>(
              result->state->controls()[4U].body()).value ==
          "synthetic command one");
}

} // namespace

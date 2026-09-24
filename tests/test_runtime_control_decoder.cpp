#include <hlclient/goldsrc/runtime_control_decoder.hpp>

#include "user_info_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace user_info_fixture = hlclient::test::user_info_fixture;

[[nodiscard]] goldsrc::OwnedServicePayload
service_payload(std::vector<std::byte> bytes) {
  goldsrc::OwnedServicePayload payload;
  payload.bytes = std::move(bytes);
  payload.source_sequence = 91U;
  payload.source_acknowledgement = 73U;
  payload.source_reliable = true;
  payload.reassembled = true;
  payload.decompressed = true;
  payload.acknowledgement_reliable = true;
  payload.direction = goldsrc::NetchanDirection::server_to_client;
  return payload;
}

[[nodiscard]] goldsrc::StockRuntimeSourceCursor
cursor(const std::size_t byte_offset, const std::size_t bit_offset,
       const std::size_t payload_size) {
  const auto value = goldsrc::StockRuntimeSourceCursor::create(
      byte_offset, bit_offset, payload_size);
  REQUIRE(value);
  return *value;
}

[[nodiscard]] goldsrc::RuntimeControlDecodeResult
decode(const goldsrc::RuntimeControlDecoder &decoder,
       const goldsrc::OwnedServicePayload &payload,
       goldsrc::RuntimeControlState &state, const std::size_t byte_offset = 0U,
       const std::size_t bit_offset = 0U,
       const std::size_t payload_ordinal = 3U,
       const std::span<const goldsrc::PostMoveVarsUserMessageDefinition>
           user_message_definitions = {}) {
  return decoder.decode_and_apply(
      goldsrc::RuntimeControlDecodeInput{
          payload,
          cursor(byte_offset, bit_offset, payload.bytes.size()),
          state.source_generation(),
          payload_ordinal,
          user_message_definitions,
      },
      state);
}

void require_error(const goldsrc::RuntimeControlDecodeResult &result,
                   const goldsrc::RuntimeControlDecodeErrorCode code) {
  INFO("expected " << goldsrc::to_string(code));
  CHECK_FALSE(result);
  CHECK_FALSE(result.batch);
  REQUIRE(result.error);
  CHECK(result.error->code == code);
  CHECK_FALSE(result.error->context.empty());
}

TEST_CASE("Reference runtime control profile is explicit and executable",
          "[goldsrc][runtime-control][profile]") {
  const goldsrc::RuntimeControlDecoder decoder;
  CHECK(decoder.valid_configuration());
  CHECK(goldsrc::valid_runtime_control_profile(decoder.profile()));
  CHECK(goldsrc::to_string(decoder.profile()) ==
        "public_goldsrc48_runtime_control_v1");

  goldsrc::RuntimeControlState state{1U};
  const auto result =
      decode(decoder, service_payload({std::byte{0x01U}}), state);
  REQUIRE(result);
  CHECK(state.committed_message_count() == 1U);

  const auto invalid =
      static_cast<goldsrc::RuntimeControlCompatibilityProfile>(0xffU);
  goldsrc::RuntimeControlState invalid_state{1U, invalid};
  const goldsrc::RuntimeControlDecoder invalid_decoder{{}, invalid};
  CHECK_FALSE(invalid_decoder.valid_configuration());
  require_error(decode(invalid_decoder, service_payload({std::byte{0x01U}}),
                       invalid_state),
                goldsrc::RuntimeControlDecodeErrorCode::invalid_profile);
}

TEST_CASE(
    "Each supported Protocol 48 runtime control message decodes literally",
    "[goldsrc][runtime-control][literal]") {
  const goldsrc::RuntimeControlDecoder decoder;

  SECTION("svc_nop") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder, service_payload({std::byte{0x01U}}), state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 1U);
    const auto &event = result.batch->events.front();
    CHECK(event.opcode == goldsrc::RuntimeControlOpcode::svc_nop);
    CHECK(event.kind == goldsrc::RuntimeControlMessageKind::nop);
    CHECK(std::holds_alternative<goldsrc::RuntimeControlNop>(event.body));
    CHECK(event.provenance.start_cursor.absolute_bit_offset() == 0U);
    CHECK(event.provenance.end_cursor.absolute_bit_offset() == 8U);
  }

  SECTION("svc_time") {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder,
        service_payload({std::byte{0x07U}, std::byte{0x00U}, std::byte{0x00U},
                         std::byte{0x48U}, std::byte{0x41U}}),
        state);
    REQUIRE(result);
    const auto body = std::get<goldsrc::RuntimeControlServerTime>(
        result.batch->events.front().body);
    CHECK(body.seconds == 12.5F);
    REQUIRE(state.server_time());
    CHECK(state.server_time()->seconds == 12.5F);
    CHECK(state.server_time()->provenance.source.source_sequence == 91U);
  }

  SECTION("svc_sound consumes the exact conditional bit body") {
    // Literal LSB-first fixture authored from the pinned ReHLDS field
    // sequence. It does not use this project's writer or decoder:
    // mask=0x0f, volume=0x12, attenuation=0x34, channel=5,
    // entity=0x155, 16-bit sound=0x2345, origin=(-291.625,0,0),
    // pitch=0x67, followed by zero byte-alignment padding and svc_nop.
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder,
        service_payload({std::byte{6U}, std::byte{0x0fU}, std::byte{0x24U},
                         std::byte{0x68U}, std::byte{0x5aU}, std::byte{0x95U},
                         std::byte{0xa2U}, std::byte{0x91U}, std::byte{0x7cU},
                         std::byte{0x24U}, std::byte{0x7aU}, std::byte{0x06U},
                         std::byte{1U}}),
        state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlSound>(
        result.batch->events.front().body);
    CHECK(body.field_mask == 0x0fU);
    CHECK(body.volume == 0x12U);
    CHECK(body.attenuation == 0x34U);
    CHECK(body.channel == 5U);
    CHECK(body.entity_reference == 0x155U);
    CHECK(body.sound_reference == 0x2345U);
    CHECK(body.origin == std::array<float, 3U>{-291.625F, 0.0F, 0.0F});
    CHECK(body.pitch == 0x67U);
    CHECK(body.encoded_body_bits == 84U);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          12U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("source-backed text and fixed controls stop at exact boundaries") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{8U}, std::byte{'o'}, std::byte{'k'},
                                std::byte{0U}, std::byte{35U}, std::byte{7U},
                                std::byte{2U}, std::byte{37U}, std::byte{0x34U},
                                std::byte{0x12U}, std::byte{1U}}),
               state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 4U);
    CHECK(result.batch->events[0].opcode ==
          goldsrc::RuntimeControlOpcode::svc_print);
    CHECK(std::get<goldsrc::RuntimeControlText>(result.batch->events[0].body)
              .text_length == 2U);
    CHECK(result.batch->events[1].opcode ==
          goldsrc::RuntimeControlOpcode::svc_weaponanim);
    CHECK(std::get<goldsrc::RuntimeControlExactFixedBody>(
              result.batch->events[1].body)
              .body_size == 2U);
    CHECK(result.batch->events[2].opcode ==
          goldsrc::RuntimeControlOpcode::svc_roomtype);
    CHECK(result.batch->events[2].provenance.end_cursor.byte_offset() == 10U);
    CHECK(result.batch->events[3].kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("svc_setview") {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder,
        service_payload({std::byte{0x05U}, std::byte{0x34U}, std::byte{0x12U}}),
        state);
    REQUIRE(result);
    const auto body = std::get<goldsrc::RuntimeControlViewEntityReference>(
        result.batch->events.front().body);
    CHECK(body.wire_entity_reference == 0x1234);
    REQUIRE(state.view_entity());
    CHECK(state.view_entity()->wire_entity_reference == 0x1234);
  }

  SECTION("svc_updateuserinfo uses the existing typed prefix parser") {
    auto bytes = user_info_fixture::exact_message();
    bytes.push_back(std::byte{0x01U});
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder, service_payload(std::move(bytes)), state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlUserInfoUpdate>(
        result.batch->events.front().body);
    CHECK(body.client_index == 2U);
    CHECK(body.info_string_length == user_info_fixture::kInfoStringLength);
    CHECK(body.info_entry_count == 4U);
    CHECK(body.opaque_suffix_size == goldsrc::kUserInfoOpaqueSuffixSize);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          user_info_fixture::kExactUserInfoMessage.size());
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("svc_lightstyle consumes only its exact NUL-terminated pattern") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{12U}, std::byte{2U}, std::byte{'a'},
                                std::byte{'b'}, std::byte{'c'}, std::byte{0U},
                                std::byte{1U}}),
               state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlLightStyle>(
        result.batch->events.front().body);
    CHECK(body.style_index == 2U);
    CHECK(body.pattern_length == 3U);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          6U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("svc_setangle consumes exactly three wire angle shorts") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{10U}, std::byte{1U}, std::byte{0U},
                                std::byte{0xfeU}, std::byte{0xffU},
                                std::byte{3U}, std::byte{0U}, std::byte{1U}}),
               state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlViewAngles>(
        result.batch->events.front().body);
    CHECK(body.angle_shorts == std::array<std::int16_t, 3U>{1, -2, 3});
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          7U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("svc_temp_entity TE_BSPDECAL without an entity model") {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder,
        service_payload({std::byte{23U}, std::byte{13U}, std::byte{8U},
                         std::byte{0U}, std::byte{0xf0U}, std::byte{0xffU},
                         std::byte{24U}, std::byte{0U}, std::byte{7U},
                         std::byte{0U}, std::byte{0U}, std::byte{0U}}),
        state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 1U);
    const auto body = std::get<goldsrc::RuntimeControlBspDecal>(
        result.batch->events.front().body);
    CHECK(body.coordinate_eighths == std::array<std::int16_t, 3U>{8, -16, 24});
    CHECK(body.decal_reference == 7);
    CHECK(body.entity_reference == 0);
    CHECK_FALSE(body.model_reference);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          12U);
  }

  SECTION("svc_temp_entity TE_BSPDECAL with an entity model") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{23U}, std::byte{13U}, std::byte{0U},
                                std::byte{0U}, std::byte{0U}, std::byte{0U},
                                std::byte{0U}, std::byte{0U}, std::byte{3U},
                                std::byte{0U}, std::byte{2U}, std::byte{0U},
                                std::byte{9U}, std::byte{0U}}),
               state);
    REQUIRE(result);
    const auto body = std::get<goldsrc::RuntimeControlBspDecal>(
        result.batch->events.front().body);
    CHECK(body.entity_reference == 2);
    REQUIRE(body.model_reference);
    CHECK(*body.model_reference == 9);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          14U);
  }

  SECTION("svc_signonnum") {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder, service_payload({std::byte{0x19U}, std::byte{0x01U}}), state);
    REQUIRE(result);
    const auto body = std::get<goldsrc::RuntimeControlSignonControl>(
        result.batch->events.front().body);
    CHECK(body.signon_number == 1U);
    REQUIRE(state.signon_control());
    CHECK(state.signon_control()->signon_number == 1U);
  }

  SECTION("svc_choke has no body and leaves the next opcode unconsumed") {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder, service_payload({std::byte{42U}, std::byte{1U}}), state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    CHECK(result.batch->events.front().kind ==
          goldsrc::RuntimeControlMessageKind::choke);
    CHECK(std::holds_alternative<goldsrc::RuntimeControlChoke>(
        result.batch->events.front().body));
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          1U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("svc_voiceinit consumes one NUL-terminated codec and quality byte") {
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{52U}, std::byte{'v'}, std::byte{'o'},
                                std::byte{'i'}, std::byte{'c'}, std::byte{'e'},
                                std::byte{0U}, std::byte{5U}, std::byte{1U}}),
               state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlVoiceInitialization>(
        result.batch->events.front().body);
    CHECK(body.codec_name_length == 5U);
    CHECK(body.quality == 5U);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          8U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION("registered fixed-size user message consumes only its exact body") {
    const std::array definitions{
        goldsrc::PostMoveVarsUserMessageDefinition{79U, 2, "Fixed"}};
    goldsrc::RuntimeControlState state{2U};
    const auto result =
        decode(decoder,
               service_payload({std::byte{79U}, std::byte{0xaaU},
                                std::byte{0xbbU}, std::byte{1U}}),
               state, 0U, 0U, 3U, definitions);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlUserMessage>(
        result.batch->events.front().body);
    CHECK(body.identifier == 79U);
    CHECK(body.declared_size == 2);
    CHECK(body.body_size == 2U);
    CHECK_FALSE(body.variable_size);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          3U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }

  SECTION(
      "registered variable-size user message uses its one-byte wire length") {
    const std::array definitions{
        goldsrc::PostMoveVarsUserMessageDefinition{80U, -1, "Variable"}};
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(
        decoder,
        service_payload({std::byte{80U}, std::byte{3U}, std::byte{0xaaU},
                         std::byte{0xbbU}, std::byte{0xccU}, std::byte{1U}}),
        state, 0U, 0U, 3U, definitions);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto body = std::get<goldsrc::RuntimeControlUserMessage>(
        result.batch->events.front().body);
    CHECK(body.identifier == 80U);
    CHECK(body.declared_size == -1);
    CHECK(body.body_size == 3U);
    CHECK(body.variable_size);
    CHECK(result.batch->events.front().provenance.end_cursor.byte_offset() ==
          5U);
    CHECK(result.batch->events.back().kind ==
          goldsrc::RuntimeControlMessageKind::nop);
  }
}

TEST_CASE("One owning payload publishes an atomic ordered control batch",
          "[goldsrc][runtime-control][sequence]") {
  auto payload = service_payload({
      std::byte{0x01U},
      std::byte{0x07U},
      std::byte{0x00U},
      std::byte{0x00U},
      std::byte{0x48U},
      std::byte{0x41U},
      std::byte{0x05U},
      std::byte{0x2aU},
      std::byte{0x00U},
      std::byte{0x19U},
      std::byte{0x01U},
  });
  goldsrc::RuntimeControlState state{9U};
  const auto result =
      decode(goldsrc::RuntimeControlDecoder{}, payload, state, 0U, 0U, 17U);

  REQUIRE(result);
  REQUIRE(result.batch->events.size() == 4U);
  CHECK(result.batch->consumed_byte_count == payload.bytes.size());
  CHECK(result.batch->consumed_bit_count == payload.bytes.size() * 8U);
  CHECK(result.batch->start_cursor.absolute_bit_offset() == 0U);
  CHECK(result.batch->end_cursor.absolute_bit_offset() ==
        payload.bytes.size() * 8U);
  CHECK(result.batch->events[0].provenance.message_ordinal == 0U);
  CHECK(result.batch->events[1].provenance.message_ordinal == 1U);
  CHECK(result.batch->events[2].provenance.message_ordinal == 2U);
  CHECK(result.batch->events[3].provenance.message_ordinal == 3U);
  CHECK(result.batch->events[3].provenance.source.payload_ordinal == 17U);
  CHECK(result.batch->events[3].provenance.source_generation == 9U);
  CHECK(state.committed_payload_count() == 1U);
  CHECK(state.committed_message_count() == 4U);
  CHECK(state.specification_source() ==
        goldsrc::RuntimeControlSpecificationSource::public_protocol_reference);
  CHECK(state.stock_verification() ==
        goldsrc::RuntimeControlStockVerification::
            not_verified_against_stock_runtime_payload);
}

TEST_CASE(
    "Runtime control state advances across payloads and resets by generation",
    "[goldsrc][runtime-control][generation]") {
  const goldsrc::RuntimeControlDecoder decoder;
  goldsrc::RuntimeControlState state{11U};

  REQUIRE(decode(
      decoder,
      service_payload({std::byte{0x07U}, std::byte{0x00U}, std::byte{0x00U},
                       std::byte{0x20U}, std::byte{0x41U}}),
      state, 0U, 0U, 1U));
  REQUIRE(decode(
      decoder,
      service_payload({std::byte{0x05U}, std::byte{0x07U}, std::byte{0x00U}}),
      state, 0U, 0U, 2U));
  REQUIRE(state.server_time());
  REQUIRE(state.view_entity());
  CHECK(state.server_time()->seconds == 10.0F);
  CHECK(state.view_entity()->wire_entity_reference == 7);
  CHECK(state.committed_payload_count() == 2U);

  const auto before_invalid_reset = state;
  CHECK_FALSE(state.reset_source_generation(0U));
  CHECK(state == before_invalid_reset);
  REQUIRE(state.reset_source_generation(12U));
  CHECK(state.source_generation() == 12U);
  CHECK_FALSE(state.server_time());
  CHECK_FALSE(state.view_entity());
  CHECK_FALSE(state.signon_control());
  CHECK(state.committed_payload_count() == 0U);
  CHECK(state.committed_message_count() == 0U);

  REQUIRE(decode(decoder, service_payload({std::byte{0x19U}, std::byte{0x01U}}),
                 state));
  REQUIRE(state.signon_control());
  CHECK(state.signon_control()->provenance.source_generation == 12U);
}

TEST_CASE("Runtime control decoder reports every truncated body boundary",
          "[goldsrc][runtime-control][truncation]") {
  const goldsrc::RuntimeControlDecoder decoder;

  SECTION("missing opcode") {
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, service_payload({}), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_opcode);
  }

  for (std::size_t body_size = 0U; body_size < 4U; ++body_size) {
    CAPTURE(body_size);
    std::vector<std::byte> bytes{std::byte{static_cast<unsigned char>(0x07U)}};
    bytes.resize(1U + body_size, std::byte{0U});
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, service_payload(std::move(bytes)), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  for (std::size_t body_size = 0U; body_size < 5U; ++body_size) {
    CAPTURE(body_size);
    std::vector<std::byte> bytes{std::byte{6U}};
    bytes.resize(1U + body_size, std::byte{0U});
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, service_payload(std::move(bytes)), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  for (std::size_t body_size = 0U; body_size < 2U; ++body_size) {
    CAPTURE(body_size);
    std::vector<std::byte> bytes{std::byte{static_cast<unsigned char>(0x05U)}};
    bytes.resize(1U + body_size, std::byte{0U});
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, service_payload(std::move(bytes)), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, service_payload({std::byte{0x19U}}), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder,
                         service_payload({std::byte{35U}, std::byte{1U}}),
                         state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder,
                         service_payload({std::byte{8U}, std::byte{'x'}}),
                         state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder,
                         service_payload({std::byte{52U}, std::byte{'v'},
                                          std::byte{'o'}, std::byte{'i'}}),
                         state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    goldsrc::RuntimeControlState state{1U};
    require_error(
        decode(decoder,
               service_payload({std::byte{52U}, std::byte{'v'}, std::byte{0U}}),
               state),
        goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    const std::array definitions{
        goldsrc::PostMoveVarsUserMessageDefinition{79U, 2, "Fixed"}};
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder,
                         service_payload({std::byte{79U}, std::byte{0xaaU}}),
                         state, 0U, 0U, 3U, definitions),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
  {
    const std::array definitions{
        goldsrc::PostMoveVarsUserMessageDefinition{80U, -1, "Variable"}};
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder,
                         service_payload(
                             {std::byte{80U}, std::byte{2U}, std::byte{0xaaU}}),
                         state, 0U, 0U, 3U, definitions),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
}

TEST_CASE("Runtime user-message catalogs are validated before decoding",
          "[goldsrc][runtime-control][user-message][transactional]") {
  const std::array duplicate_definitions{
      goldsrc::PostMoveVarsUserMessageDefinition{79U, 1, "First"},
      goldsrc::PostMoveVarsUserMessageDefinition{79U, 2, "Second"}};
  CHECK_FALSE(
      goldsrc::valid_runtime_user_message_definitions(duplicate_definitions));

  goldsrc::RuntimeControlState state{3U};
  const auto before = state;
  const auto result =
      decode(goldsrc::RuntimeControlDecoder{},
             service_payload({std::byte{79U}, std::byte{0xaaU}}), state, 0U, 0U,
             3U, duplicate_definitions);
  require_error(result,
                goldsrc::RuntimeControlDecodeErrorCode::invalid_configuration);
  CHECK(state == before);
}

TEST_CASE("Invalid runtime control numeric values fail without publication",
          "[goldsrc][runtime-control][numeric][transactional]") {
  const goldsrc::RuntimeControlDecoder decoder;
  const std::array invalid_time_bodies{
      std::array{std::byte{0x00U}, std::byte{0x00U}, std::byte{0x80U},
                 std::byte{0x7fU}},
      std::array{std::byte{0x00U}, std::byte{0x00U}, std::byte{0xc0U},
                 std::byte{0x7fU}},
  };
  for (const auto &invalid : invalid_time_bodies) {
    std::vector<std::byte> bytes{std::byte{0x07U}};
    bytes.insert(bytes.end(), invalid.begin(), invalid.end());
    goldsrc::RuntimeControlState state{3U};
    const auto before = state;
    require_error(
        decode(decoder, service_payload(std::move(bytes)), state),
        goldsrc::RuntimeControlDecodeErrorCode::invalid_numeric_value);
    CHECK(state == before);
  }
  for (const auto invalid_signon : {0U, 2U, 255U}) {
    goldsrc::RuntimeControlState state{3U};
    const auto before = state;
    require_error(
        decode(decoder,
               service_payload(
                   {std::byte{0x19U},
                    std::byte{static_cast<unsigned char>(invalid_signon)}}),
               state),
        goldsrc::RuntimeControlDecodeErrorCode::invalid_numeric_value);
    CHECK(state == before);
  }
}

TEST_CASE("Unsupported opcode stops exactly and never scans for a later match",
          "[goldsrc][runtime-control][unsupported][no-resync]") {
  auto payload = service_payload({
      std::byte{0x01U},
      std::byte{0xfeU},
      std::byte{0x07U},
      std::byte{0x00U},
      std::byte{0x00U},
      std::byte{0x80U},
      std::byte{0x3fU},
  });
  goldsrc::RuntimeControlState state{5U};
  const auto before = state;
  const auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state);
  require_error(result,
                goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
  REQUIRE(result.error->cursor);
  CHECK(result.error->cursor->absolute_bit_offset() == 8U);
  REQUIRE(result.error->wire_opcode);
  CHECK(*result.error->wire_opcode == 0xfeU);
  CHECK(state == before);
}

TEST_CASE("Unsupported temp-entity subtype stops before its unknown body",
          "[goldsrc][runtime-control][temp-entity][no-resync]") {
  auto payload = service_payload(
      {std::byte{23U}, std::byte{12U}, std::byte{0xaaU}, std::byte{22U}});
  goldsrc::RuntimeControlState state{5U};
  const auto before = state;
  const auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state);
  require_error(result, goldsrc::RuntimeControlDecodeErrorCode::
                            unsupported_temporary_entity_type);
  REQUIRE(result.error->cursor);
  CHECK(result.error->cursor->byte_offset() == 0U);
  CHECK(state == before);
}

TEST_CASE("Runtime control cursor alignment direction and generation are typed",
          "[goldsrc][runtime-control][boundary]") {
  const goldsrc::RuntimeControlDecoder decoder;

  SECTION("cursor extent belongs to a different payload") {
    auto payload = service_payload({std::byte{0x01U}});
    const auto foreign_cursor =
        goldsrc::StockRuntimeSourceCursor::create(2U, 0U, 2U);
    REQUIRE(foreign_cursor);
    goldsrc::RuntimeControlState state{1U};
    const auto before = state;
    const auto result = decoder.decode_and_apply(
        goldsrc::RuntimeControlDecodeInput{payload, *foreign_cursor, 1U, 0U},
        state);
    require_error(result,
                  goldsrc::RuntimeControlDecodeErrorCode::invalid_cursor);
    CHECK(state == before);
  }

  SECTION("unsupported bit alignment") {
    auto payload = service_payload({std::byte{0x01U}, std::byte{0x00U}});
    goldsrc::RuntimeControlState state{1U};
    const auto result = decode(decoder, payload, state, 0U, 1U);
    require_error(
        result, goldsrc::RuntimeControlDecodeErrorCode::unsupported_alignment);
    REQUIRE(result.error->cursor);
    CHECK(result.error->cursor->absolute_bit_offset() == 1U);
  }

  SECTION("wrong direction") {
    auto payload = service_payload({std::byte{0x01U}});
    payload.direction = goldsrc::NetchanDirection::client_to_server;
    goldsrc::RuntimeControlState state{1U};
    require_error(decode(decoder, payload, state),
                  goldsrc::RuntimeControlDecodeErrorCode::wrong_direction);
  }

  SECTION("payload is not a decoded service boundary") {
    auto payload = service_payload({std::byte{0x01U}});
    payload.decompressed = false;
    goldsrc::RuntimeControlState state{1U};
    require_error(
        decode(decoder, payload, state),
        goldsrc::RuntimeControlDecodeErrorCode::payload_not_decompressed);
  }

  SECTION("generation mismatch") {
    auto payload = service_payload({std::byte{0x01U}});
    goldsrc::RuntimeControlState state{4U};
    const auto result = decoder.decode_and_apply(
        goldsrc::RuntimeControlDecodeInput{
            payload, cursor(0U, 0U, payload.bytes.size()), 5U, 0U},
        state);
    require_error(
        result,
        goldsrc::RuntimeControlDecodeErrorCode::source_generation_mismatch);
  }
}

TEST_CASE(
    "Runtime control exact nonzero cursor consumes only the selected suffix",
    "[goldsrc][runtime-control][cursor]") {
  auto payload = service_payload({
      std::byte{0xeeU},
      std::byte{0x05U},
      std::byte{0x78U},
      std::byte{0x56U},
  });
  goldsrc::RuntimeControlState state{8U};
  const auto result =
      decode(goldsrc::RuntimeControlDecoder{}, payload, state, 1U);
  REQUIRE(result);
  CHECK(result.batch->consumed_byte_count == 3U);
  CHECK(result.batch->start_cursor.absolute_bit_offset() == 8U);
  CHECK(result.batch->end_cursor.absolute_bit_offset() == 32U);
  REQUIRE(state.view_entity());
  CHECK(state.view_entity()->wire_entity_reference == 0x5678);
}

TEST_CASE("Runtime control bounds fail transactionally and cursor overflow is "
          "rejected",
          "[goldsrc][runtime-control][limits][overflow]") {
  SECTION("payload bound") {
    auto limits = goldsrc::RuntimeControlDecodeLimits{};
    limits.maximum_payload_bytes = 1U;
    const goldsrc::RuntimeControlDecoder decoder{limits};
    goldsrc::RuntimeControlState state{1U};
    const auto before = state;
    require_error(decode(decoder,
                         service_payload({std::byte{0x01U}, std::byte{0x01U}}),
                         state),
                  goldsrc::RuntimeControlDecodeErrorCode::payload_too_large);
    CHECK(state == before);
  }

  SECTION("message bound after a valid prefix") {
    auto limits = goldsrc::RuntimeControlDecodeLimits{};
    limits.maximum_messages_per_payload = 1U;
    const goldsrc::RuntimeControlDecoder decoder{limits};
    goldsrc::RuntimeControlState state{1U};
    const auto before = state;
    require_error(
        decode(decoder, service_payload({std::byte{0x01U}, std::byte{0x01U}}),
               state),
        goldsrc::RuntimeControlDecodeErrorCode::message_limit_exceeded);
    CHECK(state == before);
  }

  SECTION("hard limit") {
    auto limits = goldsrc::RuntimeControlDecodeLimits{};
    limits.maximum_messages_per_payload =
        goldsrc::kMaximumRuntimeControlMessagesPerPayload + 1U;
    const goldsrc::RuntimeControlDecoder decoder{limits};
    CHECK_FALSE(decoder.valid_configuration());
    goldsrc::RuntimeControlState state{1U};
    require_error(
        decode(decoder, service_payload({std::byte{0x01U}}), state),
        goldsrc::RuntimeControlDecodeErrorCode::invalid_configuration);
  }

  CHECK_FALSE(goldsrc::StockRuntimeSourceCursor::create(
      (std::numeric_limits<std::size_t>::max)(), 7U,
      (std::numeric_limits<std::size_t>::max)()));
}

TEST_CASE("Failed later message leaves previously committed state unchanged",
          "[goldsrc][runtime-control][transactional]") {
  const goldsrc::RuntimeControlDecoder decoder;
  goldsrc::RuntimeControlState state{15U};
  REQUIRE(decode(
      decoder,
      service_payload({std::byte{0x05U}, std::byte{0x09U}, std::byte{0x00U}}),
      state));
  const auto before = state;

  const auto result = decode(decoder,
                             service_payload({
                                 std::byte{0x07U},
                                 std::byte{0x00U},
                                 std::byte{0x00U},
                                 std::byte{0x80U},
                                 std::byte{0x3fU},
                                 std::byte{0xfaU},
                             }),
                             state);
  require_error(result,
                goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
  CHECK(state == before);
}

TEST_CASE("Runtime control outputs own all values after payload destruction",
          "[goldsrc][runtime-control][ownership]") {
  goldsrc::RuntimeControlState state{21U};
  std::optional<goldsrc::RuntimeControlDecodedBatch> retained;
  {
    auto payload = service_payload({
        std::byte{0x07U},
        std::byte{0x00U},
        std::byte{0x00U},
        std::byte{0x28U},
        std::byte{0x42U},
    });
    auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state);
    REQUIRE(result);
    retained = std::move(result.batch);
    payload.bytes.assign(128U, std::byte{0xffU});
  }
  REQUIRE(retained);
  REQUIRE(retained->events.size() == 1U);
  CHECK(
      std::get<goldsrc::RuntimeControlServerTime>(retained->events.front().body)
          .seconds == 42.0F);
  REQUIRE(state.server_time());
  CHECK(state.server_time()->seconds == 42.0F);
}

TEST_CASE("Shared runtime control event application rejects variant mismatch",
          "[goldsrc][runtime-control][transactional][shared]") {
  const goldsrc::RuntimeControlDecoder decoder;
  const auto payload = service_payload({std::byte{0x01U}});
  const auto one = decoder.decode_one(
      goldsrc::RuntimeControlDecodeInput{
          payload, cursor(0U, 0U, payload.bytes.size()), 9U, 1U},
      0U);
  REQUIRE(one);
  std::array events{*one.event};
  events.front().kind = goldsrc::RuntimeControlMessageKind::server_time;

  goldsrc::RuntimeControlState state{9U};
  const auto before = state;
  const auto error = decoder.apply_events(events, state);
  REQUIRE(error);
  CHECK(error->code ==
        goldsrc::RuntimeControlDecodeErrorCode::invalid_configuration);
  CHECK(state == before);
}

} // namespace

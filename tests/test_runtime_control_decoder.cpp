#include <hlclient/goldsrc/runtime_control_decoder.hpp>

#include "user_info_test_fixture.hpp"
#include "event_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace user_info_fixture = hlclient::test::user_info_fixture;
namespace event_fixture = hlclient::test::event_fixture;

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
  REQUIRE(result.error->failure_cursor);
  CHECK(result.error->failure_cursor->byte_offset() == 2U);
  CHECK(result.error->context.find("subtype 12;") != std::string::npos);
  CHECK(state == before);
}

struct DecalFixture final {
  std::vector<std::byte> bytes;
  std::uint8_t subtype;
  std::uint16_t index;
  std::int16_t entity;
};

// Literal independent SDK layouts, including opposite entity/index ordering.
// No production encoder or body-size helper supplies these expectations.
const std::array<DecalFixture, 5U> kDecalFixtures{{
    {{std::byte{23}, std::byte{104}, std::byte{8}, std::byte{0},
      std::byte{240}, std::byte{255}, std::byte{24}, std::byte{0},
      std::byte{7}, std::byte{52}, std::byte{18}}, 104U, 7U, 4660},
    {{std::byte{23}, std::byte{109}, std::byte{8}, std::byte{0},
      std::byte{240}, std::byte{255}, std::byte{24}, std::byte{0},
      std::byte{52}, std::byte{18}, std::byte{255}}, 109U, 255U, 4660},
    {{std::byte{23}, std::byte{116}, std::byte{8}, std::byte{0},
      std::byte{240}, std::byte{255}, std::byte{24}, std::byte{0},
      std::byte{255}}, 116U, 255U, 0},
    {{std::byte{23}, std::byte{117}, std::byte{8}, std::byte{0},
      std::byte{240}, std::byte{255}, std::byte{24}, std::byte{0},
      std::byte{0}}, 117U, 256U, 0},
    {{std::byte{23}, std::byte{118}, std::byte{8}, std::byte{0},
      std::byte{240}, std::byte{255}, std::byte{24}, std::byte{0},
      std::byte{255}, std::byte{5}, std::byte{0}}, 118U, 511U, 5},
}};

TEST_CASE("Decal families consume exact owning bodies from nonzero cursors",
          "[goldsrc][runtime-control][temp-entity][literal][ownership]") {
  for (const auto &fixture : kDecalFixtures) {
    CAPTURE(fixture.subtype);
    goldsrc::RuntimeControlState state{5U};
    std::optional<goldsrc::RuntimeControlDecodedBatch> retained;
    {
      auto payload = service_payload(fixture.bytes);
      payload.bytes.insert(payload.bytes.begin(), std::byte{254});
      payload.bytes.push_back(std::byte{1}); // next svc_nop, not part of decal
      auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state, 1U);
      REQUIRE(result);
      retained = std::move(result.batch);
      payload.bytes.assign(100U, std::byte{255});
    }
    REQUIRE(retained);
    REQUIRE(retained->events.size() == 2U);
    const auto &event = retained->events.front();
    CHECK(event.kind == goldsrc::RuntimeControlMessageKind::temporary_entity);
    CHECK(event.provenance.start_cursor.byte_offset() == 1U);
    CHECK(event.provenance.end_cursor.byte_offset() == 1U + fixture.bytes.size());
    const auto &body = std::get<goldsrc::RuntimeControlDecal>(event.body);
    CHECK(body.temporary_entity_type == fixture.subtype);
    CHECK(body.coordinate_eighths == std::array<std::int16_t, 3U>{8, -16, 24});
    CHECK(body.decal_reference == fixture.index);
    CHECK(body.entity_reference == fixture.entity);
    CHECK(retained->events.back().kind == goldsrc::RuntimeControlMessageKind::nop);
    CHECK(state.committed_message_count() == 2U);
  }
}

TEST_CASE("Every truncated decal rejects the entire prefix without resync",
          "[goldsrc][runtime-control][temp-entity][transactional]") {
  for (const auto &fixture : kDecalFixtures) {
    for (std::size_t length = 1U; length < fixture.bytes.size(); ++length) {
      CAPTURE(fixture.subtype, length);
      // Valid server-time prefix would change state if partially committed.
      auto payload = service_payload({std::byte{7}, std::byte{0}, std::byte{0},
                                      std::byte{128}, std::byte{63}});
      payload.bytes.insert(payload.bytes.end(), fixture.bytes.begin(),
                           fixture.bytes.begin() + static_cast<std::ptrdiff_t>(length));
      goldsrc::RuntimeControlState state{5U};
      const auto before = state;
      const auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state);
      require_error(result, goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
      REQUIRE(result.error->cursor);
      CHECK(result.error->cursor->byte_offset() == 5U);
      REQUIRE(result.error->failure_cursor);
      CHECK(result.error->failure_cursor->byte_offset() <= payload.bytes.size());
      CHECK(state == before);
    }
    auto payload = service_payload(fixture.bytes);
    payload.bytes.push_back(std::byte{254}); // unknown suffix must remain fatal
    payload.bytes.push_back(std::byte{1});
    goldsrc::RuntimeControlState state{5U};
    const auto before = state;
    const auto result = decode(goldsrc::RuntimeControlDecoder{}, payload, state);
    require_error(result, goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    REQUIRE(result.error->cursor);
    CHECK(result.error->cursor->byte_offset() == fixture.bytes.size());
    CHECK(state == before);
  }
}

TEST_CASE("Decal event application rejects incompatible typed metadata atomically",
          "[goldsrc][runtime-control][temp-entity][transactional]") {
  const goldsrc::RuntimeControlDecoder decoder;
  auto payload = service_payload(kDecalFixtures[2].bytes);
  const auto decoded = decoder.decode_one(
      {payload, cursor(0U, 0U, payload.bytes.size()), 5U, 1U}, 0U);
  REQUIRE(decoded);
  for (unsigned mutation = 0U; mutation < 5U; ++mutation) {
    auto event = *decoded.event;
    auto &body = std::get<goldsrc::RuntimeControlDecal>(event.body);
    switch (mutation) {
    case 0U: body.temporary_entity_type = 12U; break;
    case 1U: body.entity_reference = 1; break;
    case 2U: body.decal_reference = 256U; break;
    case 3U: body.temporary_entity_type = 117U; break; // HIGH cannot carry 255
    case 4U: event.opcode = goldsrc::RuntimeControlOpcode::svc_nop; break;
    }
    goldsrc::RuntimeControlState state{5U};
    const auto before = state;
    const std::array events{event};
    const auto error = decoder.apply_events(events, state);
    REQUIRE(error);
    CHECK(error->code == goldsrc::RuntimeControlDecodeErrorCode::invalid_configuration);
    CHECK(state == before);
  }
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

TEST_CASE("Weapon animation service keeps exact sequence and body and rejects truncation",
          "[goldsrc][runtime-control][weaponanim]") {
  const goldsrc::RuntimeControlDecoder decoder;
  for (const std::uint8_t sequence : {std::uint8_t{0U}, std::uint8_t{255U}}) {
    goldsrc::RuntimeControlState state{2U};
    const auto result = decode(decoder,
        service_payload({std::byte{35U}, std::byte{sequence},
                         std::byte{7U}, std::byte{1U}}), state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto& event = result.batch->events.front();
    CHECK(event.opcode == goldsrc::RuntimeControlOpcode::svc_weaponanim);
    CHECK(event.provenance.start_cursor.byte_offset() == 0U);
    CHECK(event.provenance.end_cursor.byte_offset() == 3U);
    const auto body = std::get<goldsrc::RuntimeControlExactFixedBody>(event.body);
    CHECK(body.body_size == 2U);
  }
  for (const auto bytes : {
      std::vector<std::byte>{std::byte{35U}},
      std::vector<std::byte>{std::byte{35U}, std::byte{9U}}}) {
    goldsrc::RuntimeControlState state{2U};
    require_error(decode(decoder, service_payload(bytes), state),
                  goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
  }
}

[[nodiscard]] goldsrc::RuntimeControlDecodeResult decode_scripted_events(
    const goldsrc::RuntimeControlDecoder& decoder,
    const goldsrc::OwnedServicePayload& incoming,
    goldsrc::RuntimeControlState& state,
    const goldsrc::DeltaSchemaRegistryState* schemas = nullptr,
    const std::size_t start = 0U)
{
  return decoder.decode_and_apply({incoming,
      cursor(start, 0U, incoming.bytes.size()), state.source_generation(),
      3U, {}, schemas, 100.0}, state);
}

[[nodiscard]] std::uint32_t event_fixture_value(
    const goldsrc::RuntimeControlScriptedEvent& entry)
{
  REQUIRE(entry.arguments);
  const auto* field = entry.arguments->find_exact("fixture_value");
  REQUIRE(field);
  REQUIRE(std::holds_alternative<std::uint32_t>(field->value()));
  return std::get<std::uint32_t>(field->value());
}

TEST_CASE("Scripted event framing follows every literal optional branch and final alignment",
          "[goldsrc][runtime-control][scripted-events][literal][e9-fix]")
{
  const auto schemas = event_fixture::schemas();
  struct Literal final {
    std::vector<std::byte> bytes;
    std::size_t count, bits;
    bool reliable, packet, arguments;
    std::optional<std::uint16_t> delay;
  };
  const std::array cases{
      Literal{event_fixture::bytes(event_fixture::kEmpty), 0U, 5U, false, false, false, {}},
      Literal{event_fixture::bytes(event_fixture::kNoPacket), 1U, 17U, false, false, false, {}},
      Literal{event_fixture::bytes(event_fixture::kNoPacketDelay), 1U, 33U, false, false, false, 37U},
      Literal{event_fixture::bytes(event_fixture::kPacketNoDelta), 1U, 29U, false, true, false, {}},
      Literal{event_fixture::bytes(event_fixture::kPacketDelay), 1U, 45U, false, true, false, 37U},
      Literal{event_fixture::bytes(event_fixture::kPacketDelta), 1U, 48U, false, true, true, {}},
      Literal{event_fixture::bytes(event_fixture::kPacketDefault), 1U, 32U, false, true, true, {}},
      Literal{event_fixture::bytes(event_fixture::kReliableDefault), 1U, 14U, true, false, true, {}},
      Literal{event_fixture::bytes(event_fixture::kReliableDelta), 1U, 30U, true, false, true, {}},
      Literal{event_fixture::bytes(event_fixture::kReliableDelay), 1U, 30U, true, false, true, 37U},
  };
  for (const auto& selected : cases) {
    CAPTURE(selected.bytes.front(), selected.bits);
    goldsrc::RuntimeControlState state{7U};
    auto bytes = selected.bytes;
    bytes.insert(bytes.begin(), std::byte{254}); // supplied nonzero boundary
    bytes.push_back(std::byte{1}); // must remain the following svc_nop
    auto incoming = service_payload(std::move(bytes));
    const auto result = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, incoming, state, schemas.get(), 1U);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 2U);
    const auto& event = result.batch->events.front();
    CHECK(event.kind == goldsrc::RuntimeControlMessageKind::scripted_events);
    CHECK(event.opcode == (selected.reliable
        ? goldsrc::RuntimeControlOpcode::svc_event_reliable
        : goldsrc::RuntimeControlOpcode::svc_event));
    CHECK(event.provenance.start_cursor.byte_offset() == 1U);
    CHECK(event.provenance.end_cursor.byte_offset() == 1U + selected.bytes.size());
    const auto& body = std::get<goldsrc::RuntimeControlScriptedEvents>(event.body);
    CHECK(body.reliable == selected.reliable);
    CHECK(body.entries.size() == selected.count);
    CHECK(body.encoded_body_bits == selected.bits);
    if (selected.count) {
      const auto& entry = body.entries.front();
      CHECK(entry.event_index == (selected.packet ? 9U : 7U));
      CHECK(entry.packet_index.has_value() == selected.packet);
      if (selected.packet) CHECK(entry.packet_index == 17U);
      CHECK(static_cast<bool>(entry.arguments) == selected.arguments);
      CHECK(entry.fire_delay_ticks == selected.delay);
      if (selected.arguments)
        CHECK(event_fixture_value(entry) ==
            ((selected.bytes == event_fixture::bytes(event_fixture::kPacketDelta) ||
              selected.bytes == event_fixture::bytes(event_fixture::kReliableDelta)) ? 42U : 0U));
    }
    CHECK(result.batch->events.back().kind == goldsrc::RuntimeControlMessageKind::nop);
    CHECK(state.committed_message_count() == 2U);
  }
}

TEST_CASE("Multiple scripted events keep bit-packed entries and owning advertised delta values",
          "[goldsrc][runtime-control][scripted-events][ownership][e9-fix]")
{
  const auto schemas = event_fixture::schemas();
  std::optional<goldsrc::RuntimeControlDecodedBatch> retained;
  {
    auto incoming = service_payload(event_fixture::bytes(event_fixture::kMultiple));
    incoming.bytes.push_back(std::byte{1});
    goldsrc::RuntimeControlState state{2U};
    auto result = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, incoming, state, schemas.get());
    REQUIRE(result);
    retained = std::move(result.batch);
    incoming.bytes.assign(100U, std::byte{255});
  }
  REQUIRE(retained);
  REQUIRE(retained->events.size() == 2U);
  const auto& body = std::get<goldsrc::RuntimeControlScriptedEvents>(retained->events.front().body);
  REQUIRE(body.entries.size() == 3U);
  CHECK(body.encoded_body_bits == 88U);
  CHECK(body.entries[0].event_index == 4U);
  CHECK_FALSE(body.entries[0].packet_index);
  CHECK_FALSE(body.entries[0].arguments);
  CHECK(body.entries[0].fire_delay_ticks == 100U);
  CHECK(body.entries[1].event_index == 511U);
  CHECK(body.entries[1].packet_index == 17U);
  CHECK(event_fixture_value(body.entries[1]) == 42U);
  CHECK_FALSE(body.entries[1].fire_delay_ticks);
  CHECK(body.entries[2].event_index == 1023U);
  CHECK_FALSE(body.entries[2].arguments); // no retained value from prior entry
  CHECK(retained->events[1].provenance.start_cursor.byte_offset() == 12U);

  goldsrc::RuntimeControlState second_state{2U};
  auto repeated = service_payload(event_fixture::bytes(event_fixture::kMultiple));
  repeated.bytes.push_back(std::byte{1});
  const auto second = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, repeated, second_state, schemas.get());
  REQUIRE(second);
  const auto& copied = std::get<goldsrc::RuntimeControlScriptedEvents>(second.batch->events[0].body);
  CHECK(body == copied); // contents, not newly allocated shared_ptr identity
  CHECK(body.entries[1].arguments.get() != copied.entries[1].arguments.get());
}

TEST_CASE("Scripted event truncation and unknown suffix roll back every valid prefix",
          "[goldsrc][runtime-control][scripted-events][transactional][no-resync][e9-fix]")
{
  const auto schemas = event_fixture::schemas();
  const std::array cases{
      event_fixture::bytes(event_fixture::kEmpty),
      event_fixture::bytes(event_fixture::kNoPacket),
      event_fixture::bytes(event_fixture::kNoPacketDelay),
      event_fixture::bytes(event_fixture::kPacketNoDelta),
      event_fixture::bytes(event_fixture::kPacketDelay),
      event_fixture::bytes(event_fixture::kPacketDelta),
      event_fixture::bytes(event_fixture::kPacketDefault),
      event_fixture::bytes(event_fixture::kMaximumReferences),
      event_fixture::bytes(event_fixture::kReliableDefault),
      event_fixture::bytes(event_fixture::kReliableDelta),
      event_fixture::bytes(event_fixture::kReliableDelay),
      event_fixture::bytes(event_fixture::kMultiple),
  };
  for (const auto& selected : cases) {
    for (std::size_t size = 1U; size < selected.size(); ++size) {
      CAPTURE(selected.front(), size, selected.size());
      auto bytes = std::vector<std::byte>{std::byte{7}, std::byte{0},
          std::byte{0}, std::byte{128}, std::byte{63}};
      bytes.insert(bytes.end(), selected.begin(), selected.begin() + size);
      const auto incoming = service_payload(std::move(bytes));
      goldsrc::RuntimeControlState state{3U};
      const auto before = state;
      const auto result = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, incoming, state, schemas.get());
      REQUIRE_FALSE(result);
      REQUIRE(result.error);
      CHECK_FALSE(result.batch);
      CHECK(state == before);
      REQUIRE(result.error->cursor);
      CHECK(result.error->cursor->byte_offset() == 5U);
      REQUIRE(result.error->failure_cursor);
      CHECK(result.error->failure_cursor->absolute_bit_offset() <= incoming.bytes.size() * 8U);
    }
    auto bytes = selected;
    bytes.push_back(std::byte{254});
    bytes.push_back(std::byte{1}); // never scan to this later known opcode
    goldsrc::RuntimeControlState state{3U};
    const auto before = state;
    const auto result = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(std::move(bytes)), state, schemas.get());
    require_error(result, goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    REQUIRE(result.error->cursor);
    CHECK(result.error->cursor->byte_offset() == selected.size());
    CHECK(state == before);
  }
}

TEST_CASE("Only actual scripted deltas require event schema and remain bounded",
          "[goldsrc][runtime-control][scripted-events][limits][e9-fix]")
{
  for (const auto& bytes : {event_fixture::bytes(event_fixture::kEmpty),
      event_fixture::bytes(event_fixture::kNoPacket),
      event_fixture::bytes(event_fixture::kPacketNoDelta)}) {
    goldsrc::RuntimeControlState state{1U};
    REQUIRE(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(bytes), state));
  }
  for (const auto& bytes : {event_fixture::bytes(event_fixture::kPacketDelta),
      event_fixture::bytes(event_fixture::kReliableDefault),
      event_fixture::bytes(event_fixture::kReliableDelta)}) {
    goldsrc::RuntimeControlState state{1U};
    const auto before = state;
    require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(bytes), state),
        goldsrc::RuntimeControlDecodeErrorCode::missing_event_schema);
    CHECK(state == before);
  }
  const auto schemas = event_fixture::schemas();
  // index7, one mask byte selecting the out-of-schema bit1, followed by inert
  // zeros: a registry must not turn an invalid delta into a guessed byte skip.
  goldsrc::RuntimeControlState invalid_delta_state{1U};
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload({std::byte{21},
      std::byte{0x07}, std::byte{0x44}, std::byte{0x00}, std::byte{0x00}}),
      invalid_delta_state, schemas.get()),
      goldsrc::RuntimeControlDecodeErrorCode::event_delta_failed);
  CHECK(invalid_delta_state.committed_message_count() == 0U);

  goldsrc::RuntimeControlDecodeLimits limits;
  limits.maximum_scripted_events_per_payload = 2U;
  goldsrc::RuntimeControlState limited{1U};
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{limits},
      service_payload(event_fixture::bytes(event_fixture::kMultiple)), limited, schemas.get()),
      goldsrc::RuntimeControlDecodeErrorCode::scripted_event_limit_exceeded);
  CHECK(limited.committed_message_count() == 0U);
  auto two_messages = event_fixture::bytes(event_fixture::kMultiple);
  two_messages.insert(two_messages.end(), event_fixture::kMultiple.begin(), event_fixture::kMultiple.end());
  limits.maximum_scripted_events_per_payload = 5U;
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{limits},
      service_payload(std::move(two_messages)), limited, schemas.get()),
      goldsrc::RuntimeControlDecodeErrorCode::scripted_event_limit_exceeded);
  CHECK(limited.committed_message_count() == 0U);

  hlclient::test::delta_fixture::BitWriter writer;
  writer.write(3U, 8U);
  writer.write(31U, 5U); // maximum representable count, no invented larger count
  for (std::uint32_t index = 1U; index <= 31U; ++index) {
    writer.write(index, 10U); writer.write(0U, 1U); writer.write(0U, 1U);
  }
  writer.align_zero();
  goldsrc::RuntimeControlState maximum{1U};
  const auto largest = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(writer.bytes()), maximum);
  REQUIRE(largest);
  CHECK(std::get<goldsrc::RuntimeControlScriptedEvents>(largest.batch->events[0].body).entries.size() == 31U);
}

TEST_CASE("Scripted event typed metadata validation cannot publish forged or mis-tagged bodies",
          "[goldsrc][runtime-control][scripted-events][transactional][e9-fix]")
{
  const auto schemas = event_fixture::schemas();
  const auto incoming = service_payload(event_fixture::bytes(event_fixture::kReliableDelta));
  const auto one = goldsrc::RuntimeControlDecoder{}.decode_one({incoming,
      cursor(0U, 0U, incoming.bytes.size()), 9U, 1U, {}, schemas.get(), 100.0}, 0U);
  REQUIRE(one);
  const auto wrong_schema = goldsrc::DeltaDescriptionParser{}.parse(
      hlclient::test::delta_fixture::schema("wrong_event", event_fixture::kFields), 0U);
  REQUIRE(wrong_schema);
  auto wrong_arguments = goldsrc::DeltaObjectBuilder{{},
      goldsrc::DeltaValueCompatibilityProfile::public_goldsrc48_delta_v1}.build_default(*wrong_schema.schema);
  REQUIRE(wrong_arguments);
  const auto foreign = std::make_shared<const goldsrc::DeltaObjectState>(std::move(*wrong_arguments.state));
  for (const auto mutation : {0U, 1U, 2U, 3U, 4U, 5U}) {
    CAPTURE(mutation);
    std::array events{*one.event};
    auto& body = std::get<goldsrc::RuntimeControlScriptedEvents>(events[0].body);
    if (mutation == 0U) body.reliable = false;
    if (mutation == 1U) body.entries[0].event_index = 1024U;
    if (mutation == 2U) body.entries[0].arguments.reset();
    if (mutation == 3U) events[0].body = goldsrc::RuntimeControlNop{};
    if (mutation == 4U) body.entries[0].arguments = foreign;
    if (mutation == 5U) body.entries[0].packet_index = 2048U;
    goldsrc::RuntimeControlState state{9U};
    const auto before = state;
    const auto error = goldsrc::RuntimeControlDecoder{}.apply_events(events, state);
    REQUIRE(error);
    CHECK(error->code == goldsrc::RuntimeControlDecodeErrorCode::invalid_configuration);
    CHECK(state == before);
  }
}

TEST_CASE("Advertised scripted delta schema string and retained value budgets fail closed",
          "[goldsrc][runtime-control][scripted-events][limits][e9-fix]")
{
  namespace fixture = hlclient::test::delta_fixture;
  const auto registry = [](const std::span<const fixture::Field> fields) {
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(fixture::schema("event_t", fields), 0U);
    CAPTURE(fields.size());
    INFO((parsed.error ? parsed.error->context : std::string{}));
    REQUIRE(parsed);
    goldsrc::DeltaSchemaRegistryBuilder builder;
    REQUIRE(builder.insert(*parsed.schema));
    return std::make_shared<const goldsrc::DeltaSchemaRegistryState>(std::move(builder).publish());
  };
  std::vector<std::string> names;
  names.reserve(65U);
  for (std::size_t index = 0U; index < 65U; ++index) names.push_back("field" + std::to_string(index));
  std::vector<fixture::Field> fields;
  for (std::size_t index = 0U; index < 65U; ++index)
    fields.push_back({names[index], 0x01U, static_cast<std::uint16_t>(index), 8U});
  auto schemas = registry(fields);
  goldsrc::RuntimeControlState oversized_schema{1U};
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(event_fixture::bytes(event_fixture::kReliableDefault)),
      oversized_schema, schemas.get()), goldsrc::RuntimeControlDecodeErrorCode::event_delta_failed);
  CHECK(oversized_schema.committed_message_count() == 0U);

  const fixture::Field string_field[]{ {"fixture_string", 0x80U, 0U, 1U} };
  schemas = registry(string_field);
  fixture::BitWriter string_writer;
  string_writer.write(21U, 8U); string_writer.write(7U, 10U);
  string_writer.write(1U, 3U); string_writer.write(1U, 8U);
  string_writer.string(std::string(257U, 'x'));
  string_writer.write(0U, 1U); string_writer.align_zero();
  goldsrc::RuntimeControlState oversized_string{1U};
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(string_writer.bytes()), oversized_string, schemas.get()),
      goldsrc::RuntimeControlDecodeErrorCode::event_delta_failed);
  CHECK(oversized_string.committed_message_count() == 0U);

  fields.clear();
  for (std::size_t index = 0U; index < 56U; ++index)
    fields.push_back({names[index], 0x80U, static_cast<std::uint16_t>(index), 1U});
  schemas = registry(fields);
  fixture::BitWriter value_writer;
  value_writer.write(21U, 8U); value_writer.write(7U, 10U);
  value_writer.write(7U, 3U); // public delta can represent up to seven mask bytes
  for (std::size_t index = 0U; index < 7U; ++index) value_writer.write(255U, 8U);
  for (std::size_t index = 0U; index < 56U; ++index) value_writer.string(std::string(80U, 'x'));
  value_writer.write(0U, 1U); value_writer.align_zero();
  goldsrc::RuntimeControlState oversized_values{1U};
  require_error(decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(value_writer.bytes()), oversized_values, schemas.get()),
      goldsrc::RuntimeControlDecodeErrorCode::event_delta_failed);
  CHECK(oversized_values.committed_message_count() == 0U);

  const auto maximum = service_payload(event_fixture::bytes(event_fixture::kMaximumReferences));
  goldsrc::RuntimeControlState maximum_state{1U};
  const auto valid = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, maximum, maximum_state);
  REQUIRE(valid);
  const auto& entry = std::get<goldsrc::RuntimeControlScriptedEvents>(valid.batch->events[0].body).entries[0];
  CHECK(entry.event_index == 1023U);
  CHECK(entry.packet_index == 2047U);
}

TEST_CASE("Scripted event padding is inert and following service boundary stays exact",
          "[goldsrc][runtime-control][scripted-events][literal][e9-fix]")
{
  auto bytes = event_fixture::bytes(event_fixture::kNoPacket);
  bytes.back() = std::byte{0xfe}; // only seven high padding bits, not event flags
  bytes.push_back(std::byte{1});
  goldsrc::RuntimeControlState state{1U};
  const auto result = decode_scripted_events(goldsrc::RuntimeControlDecoder{}, service_payload(std::move(bytes)), state);
  REQUIRE(result);
  REQUIRE(result.batch->events.size() == 2U);
  CHECK(result.batch->events[1].provenance.start_cursor.byte_offset() == 4U);
}

} // namespace

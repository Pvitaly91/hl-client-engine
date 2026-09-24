#include <hlclient/goldsrc/runtime_control_decoder.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>
#include <hlclient/goldsrc/byte_reader.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] RuntimeControlDecodeResult
failure(const RuntimeControlDecodeErrorCode code, std::string context,
        std::optional<StockRuntimeSourceCursor> cursor = std::nullopt,
        std::optional<std::uint8_t> wire_opcode = std::nullopt) {
  return RuntimeControlDecodeResult{
      std::nullopt,
      RuntimeControlDecodeError{code, std::move(cursor), wire_opcode,
                                std::move(context)},
  };
}

[[nodiscard]] RuntimeControlSingleDecodeResult
single_failure(const RuntimeControlDecodeErrorCode code, std::string context,
               std::optional<StockRuntimeSourceCursor> cursor = std::nullopt,
               std::optional<std::uint8_t> wire_opcode = std::nullopt) {
  return RuntimeControlSingleDecodeResult{
      std::nullopt,
      RuntimeControlDecodeError{code, std::move(cursor), wire_opcode,
                                std::move(context)},
  };
}

[[nodiscard]] std::optional<StockRuntimeSourceCursor>
cursor_at(const std::size_t byte_offset,
          const std::size_t payload_size) noexcept {
  return StockRuntimeSourceCursor::create(byte_offset, 0U, payload_size);
}

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right,
                               std::size_t &result) noexcept {
  if (left > std::numeric_limits<std::size_t>::max() - right) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] StockRuntimeSourceMetadata
source_metadata(const RuntimeControlDecodeInput &input) noexcept {
  return StockRuntimeSourceMetadata{
      input.payload_ordinal,         input.payload.direction,
      input.payload.source_sequence, input.payload.source_acknowledgement,
      input.payload.source_reliable, input.payload.acknowledgement_reliable,
      input.payload.reassembled,     input.payload.decompressed,
      input.payload.bytes.size(),
  };
}

[[nodiscard]] bool
text_control_opcode(const RuntimeControlOpcode opcode) noexcept {
  switch (opcode) {
  case RuntimeControlOpcode::svc_print:
  case RuntimeControlOpcode::svc_stufftext:
  case RuntimeControlOpcode::svc_centerprint:
  case RuntimeControlOpcode::svc_finale:
  case RuntimeControlOpcode::svc_cutscene:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] std::optional<std::size_t>
exact_fixed_control_body_size(const RuntimeControlOpcode opcode) noexcept {
  switch (opcode) {
  case RuntimeControlOpcode::svc_stopsound:
    return 2U;
  case RuntimeControlOpcode::svc_particle:
    return 11U;
  case RuntimeControlOpcode::svc_setpause:
    return 1U;
  case RuntimeControlOpcode::svc_spawnstaticsound:
    return 14U;
  case RuntimeControlOpcode::svc_weaponanim:
    return 2U;
  case RuntimeControlOpcode::svc_roomtype:
    return 2U;
  case RuntimeControlOpcode::svc_addangle:
    return 2U;
  case RuntimeControlOpcode::svc_crosshairangle:
    return 2U;
  case RuntimeControlOpcode::svc_soundfade:
    return 4U;
  default:
    return std::nullopt;
  }
}

} // namespace

bool valid_runtime_control_profile(
    const RuntimeControlCompatibilityProfile profile) noexcept {
  return profile == RuntimeControlCompatibilityProfile::
                        public_goldsrc48_runtime_control_v1;
}

bool valid_runtime_control_decode_limits(
    const RuntimeControlDecodeLimits &limits) noexcept {
  return limits.maximum_payload_bytes != 0U &&
         limits.maximum_payload_bytes <= kMaximumRuntimeControlPayloadBytes &&
         limits.maximum_messages_per_payload != 0U &&
         limits.maximum_messages_per_payload <=
             kMaximumRuntimeControlMessagesPerPayload;
}

bool valid_runtime_user_message_definitions(
    const std::span<const PostMoveVarsUserMessageDefinition>
        definitions) noexcept {
  for (std::size_t index = 0U; index < definitions.size(); ++index) {
    const auto &definition = definitions[index];
    if (definition.identifier < 64U || definition.declared_size < -1 ||
        definition.name.empty() || definition.name.size() > 15U) {
      return false;
    }
    const auto duplicate = std::find_if(
        definitions.begin(), definitions.begin() + index,
        [&](const PostMoveVarsUserMessageDefinition &candidate) noexcept {
          return candidate.identifier == definition.identifier;
        });
    if (duplicate != definitions.begin() + index) {
      return false;
    }
  }
  return true;
}

RuntimeControlState::RuntimeControlState(
    const std::uint64_t source_generation,
    const RuntimeControlCompatibilityProfile profile) noexcept
    : source_generation_{source_generation}, profile_{profile} {}

bool RuntimeControlState::reset_source_generation(
    const std::uint64_t source_generation) noexcept {
  if (source_generation == 0U) {
    return false;
  }
  source_generation_ = source_generation;
  server_time_.reset();
  view_entity_.reset();
  signon_control_.reset();
  committed_payload_count_ = 0U;
  committed_message_count_ = 0U;
  return true;
}

std::uint64_t RuntimeControlState::source_generation() const noexcept {
  return source_generation_;
}

RuntimeControlCompatibilityProfile
RuntimeControlState::profile() const noexcept {
  return profile_;
}

RuntimeControlSpecificationSource
RuntimeControlState::specification_source() const noexcept {
  return RuntimeControlSpecificationSource::public_protocol_reference;
}

RuntimeControlStockVerification
RuntimeControlState::stock_verification() const noexcept {
  return RuntimeControlStockVerification::
      not_verified_against_stock_runtime_payload;
}

const std::optional<RuntimeControlServerTimeObservation> &
RuntimeControlState::server_time() const noexcept {
  return server_time_;
}

const std::optional<RuntimeControlViewObservation> &
RuntimeControlState::view_entity() const noexcept {
  return view_entity_;
}

const std::optional<RuntimeControlSignonObservation> &
RuntimeControlState::signon_control() const noexcept {
  return signon_control_;
}

std::size_t RuntimeControlState::committed_payload_count() const noexcept {
  return committed_payload_count_;
}

std::size_t RuntimeControlState::committed_message_count() const noexcept {
  return committed_message_count_;
}

RuntimeControlDecoder::RuntimeControlDecoder(
    const RuntimeControlDecodeLimits limits,
    const RuntimeControlCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile} {}

bool RuntimeControlDecoder::valid_configuration() const noexcept {
  return valid_runtime_control_decode_limits(limits_) &&
         valid_runtime_control_profile(profile_);
}

const RuntimeControlDecodeLimits &
RuntimeControlDecoder::limits() const noexcept {
  return limits_;
}

RuntimeControlCompatibilityProfile
RuntimeControlDecoder::profile() const noexcept {
  return profile_;
}

RuntimeControlSingleDecodeResult
RuntimeControlDecoder::decode_one(const RuntimeControlDecodeInput &input,
                                  const std::size_t message_ordinal) const {
  if (!valid_runtime_control_profile(profile_)) {
    return single_failure(RuntimeControlDecodeErrorCode::invalid_profile,
                          "unknown runtime-control compatibility profile");
  }
  if (!valid_runtime_control_decode_limits(limits_)) {
    return single_failure(RuntimeControlDecodeErrorCode::invalid_configuration,
                          "invalid runtime-control decode limits");
  }
  if (!valid_runtime_user_message_definitions(input.user_message_definitions)) {
    return single_failure(
        RuntimeControlDecodeErrorCode::invalid_configuration,
        "runtime user-message definitions are invalid or ambiguous");
  }
  if (!service_payload_decode_ready(input.payload)) {
    return single_failure(
        RuntimeControlDecodeErrorCode::payload_not_decompressed,
        "runtime control requires an owning decompressed service payload");
  }
  if (input.payload.direction != NetchanDirection::server_to_client) {
    return single_failure(
        RuntimeControlDecodeErrorCode::wrong_direction,
        "runtime control accepts only server-to-client service payloads");
  }
  if (input.payload.bytes.size() > limits_.maximum_payload_bytes) {
    return single_failure(
        RuntimeControlDecodeErrorCode::payload_too_large,
        "runtime-control service payload exceeds the configured bound");
  }
  if (!valid_stock_runtime_source_cursor(input.initial_cursor,
                                         input.payload.bytes.size())) {
    return single_failure(
        RuntimeControlDecodeErrorCode::invalid_cursor,
        "initial cursor is outside the owning service payload");
  }
  if (!input.initial_cursor.byte_aligned()) {
    return single_failure(
        RuntimeControlDecodeErrorCode::unsupported_alignment,
        "runtime-control v1 supports only byte-aligned messages",
        input.initial_cursor);
  }
  if (input.source_generation == 0U) {
    return single_failure(
        RuntimeControlDecodeErrorCode::source_generation_mismatch,
        "runtime-control source generation must be non-zero",
        input.initial_cursor);
  }

  const auto initial_byte_offset = input.initial_cursor.byte_offset();
  if (initial_byte_offset == input.payload.bytes.size()) {
    return single_failure(RuntimeControlDecodeErrorCode::truncated_opcode,
                          "runtime-control message opcode is missing",
                          input.initial_cursor);
  }

  ByteReader reader{std::span<const std::byte>{input.payload.bytes}.subspan(
      initial_byte_offset)};
  const auto wire_opcode = reader.read_uint8();
  if (!wire_opcode) {
    return single_failure(RuntimeControlDecodeErrorCode::truncated_opcode,
                          "runtime-control message opcode is truncated",
                          input.initial_cursor);
  }

  RuntimeControlMessageKind kind = RuntimeControlMessageKind::nop;
  RuntimeControlMessageBody body{RuntimeControlNop{}};
  switch (*wire_opcode) {
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_nop):
    break;
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_sound): {
    // ReHLDS Protocol 48 writes this body LSB-first and rounds the bit
    // field to the next byte. MSG_EndBitReading ignores those padding
    // values, so this decoder consumes but does not invent semantics for
    // them either.
    constexpr std::uint16_t kVolumeFlag = 1U << 0U;
    constexpr std::uint16_t kAttenuationFlag = 1U << 1U;
    constexpr std::uint16_t kLargeIndexFlag = 1U << 2U;
    constexpr std::uint16_t kPitchFlag = 1U << 3U;
    const auto body_start_bit = (initial_byte_offset + reader.position()) * 8U;
    BitReader bits{input.payload.bytes, body_start_bit};
    auto read = [&](const std::size_t width) -> std::optional<std::uint32_t> {
      const auto value = bits.read_bits(width);
      if (!value) {
        return std::nullopt;
      }
      return value.value;
    };
    const auto field_mask = read(9U);
    if (!field_mask) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_sound requires its 9-bit field mask",
                            input.initial_cursor, wire_opcode);
    }
    std::optional<std::uint8_t> volume;
    if ((*field_mask & kVolumeFlag) != 0U) {
      const auto value = read(8U);
      if (!value) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound volume field is truncated",
                              input.initial_cursor, wire_opcode);
      }
      volume = static_cast<std::uint8_t>(*value);
    }
    std::optional<std::uint8_t> attenuation;
    if ((*field_mask & kAttenuationFlag) != 0U) {
      const auto value = read(8U);
      if (!value) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound attenuation field is truncated",
                              input.initial_cursor, wire_opcode);
      }
      attenuation = static_cast<std::uint8_t>(*value);
    }
    const auto channel = read(3U);
    const auto entity = read(11U);
    const auto sound = read((*field_mask & kLargeIndexFlag) != 0U ? 16U : 8U);
    if (!channel || !entity || !sound) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "svc_sound channel/entity/sound reference is truncated",
          input.initial_cursor, wire_opcode);
    }

    std::array<float, 3U> origin{};
    std::array<bool, 3U> component_present{};
    for (auto &&present : component_present) {
      const auto value = read(1U);
      if (!value) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound origin presence mask is truncated",
                              input.initial_cursor, wire_opcode);
      }
      present = *value != 0U;
    }
    for (std::size_t component = 0U; component < component_present.size();
         ++component) {
      if (!component_present[component]) {
        continue;
      }
      const auto has_integer = read(1U);
      const auto has_fraction = read(1U);
      if (!has_integer || !has_fraction) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound bit-coordinate flags are truncated",
                              input.initial_cursor, wire_opcode);
      }
      if (*has_integer == 0U && *has_fraction == 0U) {
        continue;
      }
      const auto negative = read(1U);
      const auto integer =
          *has_integer != 0U ? read(12U) : std::optional<std::uint32_t>{0U};
      const auto fraction =
          *has_fraction != 0U ? read(3U) : std::optional<std::uint32_t>{0U};
      if (!negative || !integer || !fraction) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound bit-coordinate value is truncated",
                              input.initial_cursor, wire_opcode);
      }
      const auto magnitude =
          static_cast<float>(*integer) + static_cast<float>(*fraction) / 8.0F;
      origin[component] = *negative != 0U ? -magnitude : magnitude;
    }
    std::optional<std::uint8_t> pitch;
    if ((*field_mask & kPitchFlag) != 0U) {
      const auto value = read(8U);
      if (!value) {
        return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                              "svc_sound pitch field is truncated",
                              input.initial_cursor, wire_opcode);
      }
      pitch = static_cast<std::uint8_t>(*value);
    }
    const auto encoded_body_bits = bits.bit_offset() - body_start_bit;
    const auto padding = (8U - (bits.bit_offset() & 7U)) & 7U;
    if (padding != 0U && !bits.read_bits(padding)) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_sound byte-alignment padding is truncated",
                            input.initial_cursor, wire_opcode);
    }
    const auto body_bytes = (bits.bit_offset() - body_start_bit) / 8U;
    if (!reader.read_bytes(body_bytes)) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_sound rounded body exceeds its owning payload",
                            input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::sound;
    body = RuntimeControlSound{static_cast<std::uint16_t>(*field_mask),
                               volume,
                               attenuation,
                               static_cast<std::uint8_t>(*channel),
                               static_cast<std::uint16_t>(*entity),
                               static_cast<std::uint16_t>(*sound),
                               origin,
                               pitch,
                               encoded_body_bits};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_print):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_stufftext):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_centerprint):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_finale):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_cutscene): {
    const auto remaining =
        std::span<const std::byte>{input.payload.bytes}.subspan(
            initial_byte_offset + reader.position());
    const auto terminator =
        std::find(remaining.begin(), remaining.end(), std::byte{0U});
    if (terminator == remaining.end()) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "runtime text control has no in-payload NUL terminator",
          input.initial_cursor, wire_opcode);
    }
    const auto text_length =
        static_cast<std::size_t>(std::distance(remaining.begin(), terminator));
    if (!reader.read_bytes(text_length + 1U)) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "runtime text control exceeds its owning payload",
                            input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::text_control;
    body = RuntimeControlText{text_length};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_stopsound):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_particle):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_setpause):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_spawnstaticsound):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_weaponanim):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_roomtype):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_addangle):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_crosshairangle):
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_soundfade): {
    const auto body_size = exact_fixed_control_body_size(
        static_cast<RuntimeControlOpcode>(*wire_opcode));
    if (!body_size || !reader.read_bytes(*body_size)) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "exact fixed-size runtime control body is truncated",
          input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::exact_fixed_control;
    body = RuntimeControlExactFixedBody{*body_size};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_time): {
    const auto value = reader.read_float32_le();
    if (!value) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_time requires one little-endian float32 body",
                            input.initial_cursor, wire_opcode);
    }
    if (!std::isfinite(*value)) {
      return single_failure(
          RuntimeControlDecodeErrorCode::invalid_numeric_value,
          "svc_time requires a finite float32 value", input.initial_cursor,
          wire_opcode);
    }
    kind = RuntimeControlMessageKind::server_time;
    body = RuntimeControlServerTime{*value};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_setview): {
    const auto value = reader.read_int16_le();
    if (!value) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_setview requires one little-endian int16 body",
                            input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::view_entity_reference;
    body = RuntimeControlViewEntityReference{*value};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_setangle): {
    std::array<std::int16_t, 3U> angles{};
    for (auto &angle : angles) {
      const auto value = reader.read_int16_le();
      if (!value) {
        return single_failure(
            RuntimeControlDecodeErrorCode::truncated_body,
            "svc_setangle requires three little-endian int16 angles",
            input.initial_cursor, wire_opcode);
      }
      angle = *value;
    }
    kind = RuntimeControlMessageKind::view_angles;
    body = RuntimeControlViewAngles{angles};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_lightstyle): {
    const auto style_index = reader.read_uint8();
    if (!style_index) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_lightstyle requires a style-index byte",
                            input.initial_cursor, wire_opcode);
    }
    const auto remaining =
        std::span<const std::byte>{input.payload.bytes}.subspan(
            initial_byte_offset + reader.position());
    const auto terminator =
        std::find(remaining.begin(), remaining.end(), std::byte{0U});
    if (terminator == remaining.end()) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "svc_lightstyle pattern has no in-payload NUL terminator",
          input.initial_cursor, wire_opcode);
    }
    const auto pattern_length =
        static_cast<std::size_t>(std::distance(remaining.begin(), terminator));
    if (!reader.read_bytes(pattern_length + 1U)) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_lightstyle pattern exceeds its owning payload",
                            input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::light_style;
    body = RuntimeControlLightStyle{*style_index, pattern_length};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_updateuserinfo): {
    const auto parsed = UserInfoUpdateParser{}.parse_prefix(
        input.payload.bytes, initial_byte_offset);
    if (!parsed || !parsed.state) {
      const auto error_cursor = cursor_at(
          parsed.error ? parsed.error->byte_offset : initial_byte_offset,
          input.payload.bytes.size());
      return single_failure(
          RuntimeControlDecodeErrorCode::user_info_update_failed,
          parsed.error ? "svc_updateuserinfo typed parser failed: " +
                             std::string{to_string(parsed.error->code)}
                       : "svc_updateuserinfo typed parser returned no state",
          error_cursor ? error_cursor : std::optional{input.initial_cursor},
          wire_opcode);
    }
    const auto remaining_body_bytes = parsed.bytes_consumed - 1U;
    if (!reader.read_bytes(remaining_body_bytes)) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "svc_updateuserinfo typed body exceeds its owning payload",
          input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::user_info_update;
    body = RuntimeControlUserInfoUpdate{
        parsed.state->client_index(), parsed.state->info_string_length(),
        parsed.state->info_entry_count(), parsed.state->opaque_suffix_size()};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_temp_entity): {
    constexpr std::uint8_t kBspDecalType = 13U;
    const auto type = reader.read_uint8();
    if (!type) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_temp_entity requires a subtype",
                            input.initial_cursor, wire_opcode);
    }
    if (*type != kBspDecalType) {
      return single_failure(
          RuntimeControlDecodeErrorCode::unsupported_temporary_entity_type,
          "unsupported svc_temp_entity subtype; body length was not guessed",
          input.initial_cursor, wire_opcode);
    }
    std::array<std::int16_t, 3U> coordinates{};
    for (auto &coordinate : coordinates) {
      const auto value = reader.read_int16_le();
      if (!value) {
        return single_failure(
            RuntimeControlDecodeErrorCode::truncated_body,
            "TE_BSPDECAL requires three Protocol 48 coordinate shorts",
            input.initial_cursor, wire_opcode);
      }
      coordinate = *value;
    }
    const auto decal = reader.read_int16_le();
    const auto entity = reader.read_int16_le();
    if (!decal || !entity) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "TE_BSPDECAL requires decal and entity references",
                            input.initial_cursor, wire_opcode);
    }
    std::optional<std::int16_t> model;
    if (*entity != 0) {
      model = reader.read_int16_le();
      if (!model) {
        return single_failure(
            RuntimeControlDecodeErrorCode::truncated_body,
            "TE_BSPDECAL with an entity requires a model reference",
            input.initial_cursor, wire_opcode);
      }
    }
    kind = RuntimeControlMessageKind::temporary_entity;
    body = RuntimeControlBspDecal{*type, coordinates, *decal, *entity, model};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_signonnum): {
    const auto value = reader.read_uint8();
    if (!value) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_signonnum requires one uint8 body",
                            input.initial_cursor, wire_opcode);
    }
    if (*value != 1U) {
      return single_failure(
          RuntimeControlDecodeErrorCode::invalid_numeric_value,
          "runtime-control v1 supports the referenced signon value 1",
          input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::signon_control;
    body = RuntimeControlSignonControl{*value};
    break;
  }
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_choke):
    kind = RuntimeControlMessageKind::choke;
    body = RuntimeControlChoke{};
    break;
  case static_cast<std::uint8_t>(RuntimeControlOpcode::svc_voiceinit): {
    const auto remaining =
        std::span<const std::byte>{input.payload.bytes}.subspan(
            initial_byte_offset + reader.position());
    const auto terminator =
        std::find(remaining.begin(), remaining.end(), std::byte{0U});
    if (terminator == remaining.end()) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "svc_voiceinit codec has no in-payload NUL terminator",
          input.initial_cursor, wire_opcode);
    }
    const auto codec_name_length =
        static_cast<std::size_t>(std::distance(remaining.begin(), terminator));
    if (!reader.read_bytes(codec_name_length + 1U)) {
      return single_failure(RuntimeControlDecodeErrorCode::truncated_body,
                            "svc_voiceinit codec exceeds its owning payload",
                            input.initial_cursor, wire_opcode);
    }
    const auto quality = reader.read_uint8();
    if (!quality) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "svc_voiceinit requires a quality byte after its codec",
          input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::voice_initialization;
    body = RuntimeControlVoiceInitialization{codec_name_length, *quality};
    break;
  }
  default:
    const auto definition = std::find_if(
        input.user_message_definitions.begin(),
        input.user_message_definitions.end(),
        [&](const PostMoveVarsUserMessageDefinition &candidate) noexcept {
          return candidate.identifier == *wire_opcode;
        });
    if (definition == input.user_message_definitions.end()) {
      return single_failure(
          RuntimeControlDecodeErrorCode::unsupported_opcode,
          "unsupported runtime-control opcode; body length was not guessed",
          input.initial_cursor, wire_opcode);
    }
    std::size_t body_size = 0U;
    const bool variable_size = definition->declared_size == -1;
    if (variable_size) {
      const auto wire_size = reader.read_uint8();
      if (!wire_size) {
        return single_failure(
            RuntimeControlDecodeErrorCode::truncated_body,
            "variable registered user message requires a length byte",
            input.initial_cursor, wire_opcode);
      }
      body_size = *wire_size;
    } else {
      body_size = static_cast<std::size_t>(definition->declared_size);
    }
    if (!reader.read_bytes(body_size)) {
      return single_failure(
          RuntimeControlDecodeErrorCode::truncated_body,
          "registered user-message body exceeds its owning payload",
          input.initial_cursor, wire_opcode);
    }
    kind = RuntimeControlMessageKind::user_message;
    body = RuntimeControlUserMessage{definition->identifier,
                                     definition->declared_size, body_size,
                                     variable_size};
    break;
  }

  std::size_t end_byte = 0U;
  if (!checked_add(initial_byte_offset, reader.position(), end_byte)) {
    return single_failure(RuntimeControlDecodeErrorCode::size_overflow,
                          "runtime-control end cursor overflowed",
                          input.initial_cursor, wire_opcode);
  }
  const auto end_cursor = cursor_at(end_byte, input.payload.bytes.size());
  if (!end_cursor) {
    return single_failure(RuntimeControlDecodeErrorCode::size_overflow,
                          "runtime-control end cursor cannot be represented",
                          input.initial_cursor, wire_opcode);
  }
  const auto provenance = RuntimeControlMessageProvenance{
      input.source_generation,
      source_metadata(input),
      input.initial_cursor,
      *end_cursor,
      message_ordinal,
      profile_,
      RuntimeControlSpecificationSource::public_protocol_reference,
      RuntimeControlStockVerification::
          not_verified_against_stock_runtime_payload,
  };
  return RuntimeControlSingleDecodeResult{
      RuntimeControlEvent{static_cast<RuntimeControlOpcode>(*wire_opcode), kind,
                          provenance, std::move(body)},
      std::nullopt};
}

std::optional<RuntimeControlDecodeError> RuntimeControlDecoder::apply_events(
    const std::span<const RuntimeControlEvent> events,
    RuntimeControlState &state, const bool count_payload) const {
  if (!valid_configuration() || state.profile_ != profile_) {
    return RuntimeControlDecodeError{
        RuntimeControlDecodeErrorCode::invalid_profile, std::nullopt,
        std::nullopt, "runtime-control state and decoder profiles differ"};
  }
  RuntimeControlState next = state;
  for (const auto &event : events) {
    if (event.provenance.source_generation != state.source_generation_ ||
        event.provenance.profile != profile_) {
      return RuntimeControlDecodeError{
          RuntimeControlDecodeErrorCode::source_generation_mismatch,
          event.provenance.start_cursor,
          static_cast<std::uint8_t>(event.opcode),
          "decoded control event does not belong to the staged generation"};
    }
    switch (event.kind) {
    case RuntimeControlMessageKind::nop:
      if (!std::holds_alternative<RuntimeControlNop>(event.body) ||
          event.opcode != RuntimeControlOpcode::svc_nop) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded nop event has an incompatible opcode or body"};
      }
      break;
    case RuntimeControlMessageKind::sound: {
      const auto *body = std::get_if<RuntimeControlSound>(&event.body);
      constexpr std::uint16_t kLargeIndexFlag = 1U << 2U;
      constexpr std::uint16_t kVolumeFlag = 1U << 0U;
      constexpr std::uint16_t kAttenuationFlag = 1U << 1U;
      constexpr std::uint16_t kPitchFlag = 1U << 3U;
      if (body == nullptr || event.opcode != RuntimeControlOpcode::svc_sound ||
          body->field_mask > 0x01ffU || body->channel > 7U ||
          body->entity_reference > 0x07ffU ||
          ((body->field_mask & kLargeIndexFlag) == 0U &&
           body->sound_reference > 0xffU) ||
          body->volume.has_value() !=
              ((body->field_mask & kVolumeFlag) != 0U) ||
          body->attenuation.has_value() !=
              ((body->field_mask & kAttenuationFlag) != 0U) ||
          body->pitch.has_value() != ((body->field_mask & kPitchFlag) != 0U) ||
          body->encoded_body_bits == 0U ||
          !std::all_of(body->origin.begin(), body->origin.end(),
                       [](const float value) noexcept {
                         return std::isfinite(value);
                       })) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded sound event has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::text_control: {
      const auto *body = std::get_if<RuntimeControlText>(&event.body);
      if (body == nullptr || !text_control_opcode(event.opcode) ||
          body->text_length > limits_.maximum_payload_bytes) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded text control has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::exact_fixed_control: {
      const auto *body = std::get_if<RuntimeControlExactFixedBody>(&event.body);
      const auto expected = exact_fixed_control_body_size(event.opcode);
      if (body == nullptr || !expected || body->body_size != *expected) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded fixed control has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::server_time: {
      const auto *body = std::get_if<RuntimeControlServerTime>(&event.body);
      if (body == nullptr || event.opcode != RuntimeControlOpcode::svc_time) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded server-time event has an incompatible opcode or body"};
      }
      next.server_time_ =
          RuntimeControlServerTimeObservation{body->seconds, event.provenance};
      break;
    }
    case RuntimeControlMessageKind::view_entity_reference: {
      const auto *body =
          std::get_if<RuntimeControlViewEntityReference>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_setview) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded view event has an incompatible opcode or body"};
      }
      next.view_entity_ = RuntimeControlViewObservation{
          body->wire_entity_reference, event.provenance};
      break;
    }
    case RuntimeControlMessageKind::view_angles: {
      const auto *body = std::get_if<RuntimeControlViewAngles>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_setangle) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded view-angle event has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::light_style: {
      const auto *body = std::get_if<RuntimeControlLightStyle>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_lightstyle) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded light-style event has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::user_info_update: {
      const auto *body = std::get_if<RuntimeControlUserInfoUpdate>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_updateuserinfo ||
          body->client_index > kMaximumUserInfoClientIndex ||
          body->info_string_length == 0U || body->info_entry_count == 0U ||
          body->opaque_suffix_size != kUserInfoOpaqueSuffixSize) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded user-info event has incompatible typed metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::temporary_entity: {
      const auto *body = std::get_if<RuntimeControlBspDecal>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_temp_entity ||
          body->temporary_entity_type != 13U ||
          (body->entity_reference == 0 && body->model_reference) ||
          (body->entity_reference != 0 && !body->model_reference)) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded TE_BSPDECAL event has incompatible typed fields"};
      }
      break;
    }
    case RuntimeControlMessageKind::signon_control: {
      const auto *body = std::get_if<RuntimeControlSignonControl>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_signonnum) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded signon event has an incompatible opcode or body"};
      }
      next.signon_control_ = RuntimeControlSignonObservation{
          body->signon_number, event.provenance};
      break;
    }
    case RuntimeControlMessageKind::choke:
      if (!std::holds_alternative<RuntimeControlChoke>(event.body) ||
          event.opcode != RuntimeControlOpcode::svc_choke) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded choke event has an incompatible opcode or body"};
      }
      break;
    case RuntimeControlMessageKind::voice_initialization: {
      const auto *body =
          std::get_if<RuntimeControlVoiceInitialization>(&event.body);
      if (body == nullptr ||
          event.opcode != RuntimeControlOpcode::svc_voiceinit ||
          body->codec_name_length > limits_.maximum_payload_bytes) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded voice-initialization event has incompatible typed "
            "metadata"};
      }
      break;
    }
    case RuntimeControlMessageKind::user_message: {
      const auto *body = std::get_if<RuntimeControlUserMessage>(&event.body);
      if (body == nullptr ||
          body->identifier != static_cast<std::uint8_t>(event.opcode) ||
          body->identifier < 64U || body->declared_size < -1 ||
          body->variable_size != (body->declared_size == -1) ||
          (!body->variable_size &&
           body->body_size != static_cast<std::size_t>(body->declared_size))) {
        return RuntimeControlDecodeError{
            RuntimeControlDecodeErrorCode::invalid_configuration,
            event.provenance.start_cursor,
            static_cast<std::uint8_t>(event.opcode),
            "decoded user-message event has incompatible typed metadata"};
      }
      break;
    }
    }
  }
  std::size_t next_messages = 0U;
  std::size_t next_payloads = state.committed_payload_count_;
  if (!checked_add(state.committed_message_count_, events.size(),
                   next_messages) ||
      (count_payload && !checked_add(next_payloads, 1U, next_payloads))) {
    return RuntimeControlDecodeError{
        RuntimeControlDecodeErrorCode::size_overflow, std::nullopt,
        std::nullopt, "runtime-control committed-state counter overflowed"};
  }
  next.committed_message_count_ = next_messages;
  next.committed_payload_count_ = next_payloads;
  state = std::move(next);
  return std::nullopt;
}

RuntimeControlDecodeResult
RuntimeControlDecoder::decode_and_apply(const RuntimeControlDecodeInput &input,
                                        RuntimeControlState &state) const {
  if (state.source_generation_ != input.source_generation) {
    return failure(
        RuntimeControlDecodeErrorCode::source_generation_mismatch,
        "payload source generation does not match runtime control state",
        input.initial_cursor);
  }
  if (state.profile_ != profile_) {
    return failure(RuntimeControlDecodeErrorCode::invalid_profile,
                   "runtime control state and decoder profiles differ",
                   input.initial_cursor);
  }
  if (!valid_stock_runtime_source_cursor(input.initial_cursor,
                                         input.payload.bytes.size()) ||
      !input.initial_cursor.byte_aligned()) {
    const auto one = decode_one(input, 0U);
    return RuntimeControlDecodeResult{std::nullopt, one.error};
  }
  if (input.initial_cursor.byte_offset() == input.payload.bytes.size()) {
    const auto one = decode_one(input, 0U);
    return RuntimeControlDecodeResult{std::nullopt, one.error};
  }
  std::vector<RuntimeControlEvent> events;
  try {
    const auto remaining_bytes =
        input.payload.bytes.size() - input.initial_cursor.byte_offset();
    events.reserve(
        (std::min)(remaining_bytes, limits_.maximum_messages_per_payload));
  } catch (const std::bad_alloc &) {
    return failure(RuntimeControlDecodeErrorCode::unable_to_retain_output,
                   "unable to reserve runtime-control event output");
  }

  auto current = input.initial_cursor;
  while (current.byte_offset() != input.payload.bytes.size()) {
    if (events.size() >= limits_.maximum_messages_per_payload) {
      return failure(
          RuntimeControlDecodeErrorCode::message_limit_exceeded,
          "runtime-control message count exceeds the configured bound",
          current);
    }
    const auto one =
        decode_one(RuntimeControlDecodeInput{input.payload, current,
                                             input.source_generation,
                                             input.payload_ordinal,
                                             input.user_message_definitions},
                   events.size());
    if (!one) {
      return RuntimeControlDecodeResult{std::nullopt, one.error};
    }
    try {
      events.push_back(*one.event);
    } catch (const std::bad_alloc &) {
      return failure(RuntimeControlDecodeErrorCode::unable_to_retain_output,
                     "unable to retain runtime-control event output", current);
    }
    current = one.event->provenance.end_cursor;
  }
  if (const auto apply_error = apply_events(events, state)) {
    return RuntimeControlDecodeResult{std::nullopt, apply_error};
  }
  const auto consumed_bytes =
      input.payload.bytes.size() - input.initial_cursor.byte_offset();
  if (consumed_bytes > std::numeric_limits<std::size_t>::max() / 8U) {
    return failure(RuntimeControlDecodeErrorCode::size_overflow,
                   "runtime-control consumed bit count overflowed");
  }

  RuntimeControlDecodedBatch batch{
      std::move(events),
      input.initial_cursor,
      current,
      consumed_bytes,
      consumed_bytes * 8U,
      input.source_generation,
      input.payload_ordinal,
      profile_,
      RuntimeControlSpecificationSource::public_protocol_reference,
      RuntimeControlStockVerification::
          not_verified_against_stock_runtime_payload,
  };
  return RuntimeControlDecodeResult{std::move(batch), std::nullopt};
}

std::string_view
to_string(const RuntimeControlCompatibilityProfile profile) noexcept {
  switch (profile) {
  case RuntimeControlCompatibilityProfile::public_goldsrc48_runtime_control_v1:
    return "public_goldsrc48_runtime_control_v1";
  }
  return "unknown";
}

std::string_view
to_string(const RuntimeControlSpecificationSource source) noexcept {
  switch (source) {
  case RuntimeControlSpecificationSource::public_protocol_reference:
    return "public_protocol_reference";
  }
  return "unknown";
}

std::string_view
to_string(const RuntimeControlStockVerification verification) noexcept {
  switch (verification) {
  case RuntimeControlStockVerification::
      not_verified_against_stock_runtime_payload:
    return "not_verified_against_stock_runtime_payload";
  }
  return "unknown";
}

std::string_view to_string(const RuntimeControlOpcode opcode) noexcept {
  switch (opcode) {
  case RuntimeControlOpcode::svc_nop:
    return "svc_nop";
  case RuntimeControlOpcode::svc_setview:
    return "svc_setview";
  case RuntimeControlOpcode::svc_sound:
    return "svc_sound";
  case RuntimeControlOpcode::svc_time:
    return "svc_time";
  case RuntimeControlOpcode::svc_print:
    return "svc_print";
  case RuntimeControlOpcode::svc_stufftext:
    return "svc_stufftext";
  case RuntimeControlOpcode::svc_setangle:
    return "svc_setangle";
  case RuntimeControlOpcode::svc_lightstyle:
    return "svc_lightstyle";
  case RuntimeControlOpcode::svc_updateuserinfo:
    return "svc_updateuserinfo";
  case RuntimeControlOpcode::svc_stopsound:
    return "svc_stopsound";
  case RuntimeControlOpcode::svc_particle:
    return "svc_particle";
  case RuntimeControlOpcode::svc_temp_entity:
    return "svc_temp_entity";
  case RuntimeControlOpcode::svc_setpause:
    return "svc_setpause";
  case RuntimeControlOpcode::svc_signonnum:
    return "svc_signonnum";
  case RuntimeControlOpcode::svc_centerprint:
    return "svc_centerprint";
  case RuntimeControlOpcode::svc_spawnstaticsound:
    return "svc_spawnstaticsound";
  case RuntimeControlOpcode::svc_finale:
    return "svc_finale";
  case RuntimeControlOpcode::svc_cutscene:
    return "svc_cutscene";
  case RuntimeControlOpcode::svc_weaponanim:
    return "svc_weaponanim";
  case RuntimeControlOpcode::svc_roomtype:
    return "svc_roomtype";
  case RuntimeControlOpcode::svc_addangle:
    return "svc_addangle";
  case RuntimeControlOpcode::svc_choke:
    return "svc_choke";
  case RuntimeControlOpcode::svc_crosshairangle:
    return "svc_crosshairangle";
  case RuntimeControlOpcode::svc_soundfade:
    return "svc_soundfade";
  case RuntimeControlOpcode::svc_voiceinit:
    return "svc_voiceinit";
  }
  return "unknown";
}

std::string_view to_string(const RuntimeControlMessageKind kind) noexcept {
  switch (kind) {
  case RuntimeControlMessageKind::nop:
    return "nop";
  case RuntimeControlMessageKind::sound:
    return "sound";
  case RuntimeControlMessageKind::text_control:
    return "text_control";
  case RuntimeControlMessageKind::exact_fixed_control:
    return "exact_fixed_control";
  case RuntimeControlMessageKind::server_time:
    return "server_time";
  case RuntimeControlMessageKind::view_entity_reference:
    return "view_entity_reference";
  case RuntimeControlMessageKind::view_angles:
    return "view_angles";
  case RuntimeControlMessageKind::light_style:
    return "light_style";
  case RuntimeControlMessageKind::user_info_update:
    return "user_info_update";
  case RuntimeControlMessageKind::temporary_entity:
    return "temporary_entity";
  case RuntimeControlMessageKind::signon_control:
    return "signon_control";
  case RuntimeControlMessageKind::choke:
    return "choke";
  case RuntimeControlMessageKind::voice_initialization:
    return "voice_initialization";
  case RuntimeControlMessageKind::user_message:
    return "user_message";
  }
  return "unknown";
}

std::string_view to_string(const RuntimeControlDecodeErrorCode code) noexcept {
  switch (code) {
  case RuntimeControlDecodeErrorCode::invalid_configuration:
    return "invalid_configuration";
  case RuntimeControlDecodeErrorCode::invalid_profile:
    return "invalid_profile";
  case RuntimeControlDecodeErrorCode::payload_not_decompressed:
    return "payload_not_decompressed";
  case RuntimeControlDecodeErrorCode::wrong_direction:
    return "wrong_direction";
  case RuntimeControlDecodeErrorCode::payload_too_large:
    return "payload_too_large";
  case RuntimeControlDecodeErrorCode::invalid_cursor:
    return "invalid_cursor";
  case RuntimeControlDecodeErrorCode::unsupported_alignment:
    return "unsupported_alignment";
  case RuntimeControlDecodeErrorCode::source_generation_mismatch:
    return "source_generation_mismatch";
  case RuntimeControlDecodeErrorCode::truncated_opcode:
    return "truncated_opcode";
  case RuntimeControlDecodeErrorCode::truncated_body:
    return "truncated_body";
  case RuntimeControlDecodeErrorCode::invalid_numeric_value:
    return "invalid_numeric_value";
  case RuntimeControlDecodeErrorCode::user_info_update_failed:
    return "user_info_update_failed";
  case RuntimeControlDecodeErrorCode::unsupported_temporary_entity_type:
    return "unsupported_temporary_entity_type";
  case RuntimeControlDecodeErrorCode::unsupported_opcode:
    return "unsupported_opcode";
  case RuntimeControlDecodeErrorCode::message_limit_exceeded:
    return "message_limit_exceeded";
  case RuntimeControlDecodeErrorCode::size_overflow:
    return "size_overflow";
  case RuntimeControlDecodeErrorCode::unable_to_retain_output:
    return "unable_to_retain_output";
  }
  return "unknown";
}

} // namespace hlclient::goldsrc

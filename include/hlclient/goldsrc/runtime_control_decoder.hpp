#pragma once

#include <hlclient/goldsrc/move_vars.hpp>
#include <hlclient/goldsrc/delta_value_decoder.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/stock_runtime_message_catalog.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {

// This profile is executable from public Protocol 48 references and literal
// project-owned fixtures. It is deliberately separate from the accepted-stock
// evidence profiles in stock_runtime_message_catalog.hpp.
enum class RuntimeControlCompatibilityProfile : std::uint8_t {
  public_goldsrc48_runtime_control_v1,
};

enum class RuntimeControlSpecificationSource : std::uint8_t {
  public_protocol_reference,
};

enum class RuntimeControlStockVerification : std::uint8_t {
  not_verified_against_stock_runtime_payload,
};

[[nodiscard]] bool valid_runtime_control_profile(
    RuntimeControlCompatibilityProfile profile) noexcept;

enum class RuntimeControlOpcode : std::uint8_t {
  svc_nop = 1U,
  svc_event = 3U,
  svc_setview = 5U,
  svc_sound = 6U,
  svc_time = 7U,
  svc_print = 8U,
  svc_stufftext = 9U,
  svc_setangle = 10U,
  svc_lightstyle = 12U,
  svc_updateuserinfo = 13U,
  svc_stopsound = 16U,
  svc_particle = 18U,
  svc_event_reliable = 21U,
  svc_temp_entity = 23U,
  svc_setpause = 24U,
  svc_signonnum = 25U,
  svc_centerprint = 26U,
  svc_spawnstaticsound = 29U,
  svc_finale = 32U,
  svc_cutscene = 34U,
  svc_weaponanim = 35U,
  svc_roomtype = 37U,
  svc_addangle = 38U,
  svc_choke = 42U,
  svc_crosshairangle = 47U,
  svc_soundfade = 48U,
  svc_voiceinit = 52U,
};

enum class RuntimeControlMessageKind : std::uint8_t {
  nop,
  sound,
  text_control,
  exact_fixed_control,
  server_time,
  view_entity_reference,
  view_angles,
  light_style,
  user_info_update,
  temporary_entity,
  signon_control,
  choke,
  voice_initialization,
  user_message,
  scripted_events,
};

struct RuntimeControlDecodeLimits final {
  // Project safety limits, not claims about engine maxima.
  std::size_t maximum_payload_bytes{65'536U};
  std::size_t maximum_messages_per_payload{256U};
  std::size_t maximum_scripted_events_per_payload{128U};
};

inline constexpr std::size_t kMaximumRuntimeControlPayloadBytes = 1U << 20U;
inline constexpr std::size_t kMaximumRuntimeControlMessagesPerPayload = 512U;
inline constexpr std::size_t kMaximumRuntimeControlScriptedEventsPerPayload = 128U;

[[nodiscard]] bool valid_runtime_control_decode_limits(
    const RuntimeControlDecodeLimits &limits) noexcept;
[[nodiscard]] bool valid_runtime_user_message_definitions(
    std::span<const PostMoveVarsUserMessageDefinition> definitions) noexcept;

struct RuntimeControlMessageProvenance final {
  std::uint64_t source_generation{0U};
  StockRuntimeSourceMetadata source{};
  StockRuntimeSourceCursor start_cursor{};
  StockRuntimeSourceCursor end_cursor{};
  std::size_t message_ordinal{0U};
  RuntimeControlCompatibilityProfile profile{
      RuntimeControlCompatibilityProfile::public_goldsrc48_runtime_control_v1};
  RuntimeControlSpecificationSource specification_source{
      RuntimeControlSpecificationSource::public_protocol_reference};
  RuntimeControlStockVerification stock_verification{
      RuntimeControlStockVerification::
          not_verified_against_stock_runtime_payload};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlMessageProvenance &,
             const RuntimeControlMessageProvenance &) = default;
};

struct RuntimeControlNop final {
  [[nodiscard]] friend bool operator==(const RuntimeControlNop &,
                                       const RuntimeControlNop &) = default;
};

struct RuntimeControlChoke final {
  [[nodiscard]] friend bool operator==(const RuntimeControlChoke &,
                                       const RuntimeControlChoke &) = default;
};

struct RuntimeControlSound final {
  std::uint16_t field_mask{0U};
  std::optional<std::uint8_t> volume{};
  std::optional<std::uint8_t> attenuation{};
  std::uint8_t channel{0U};
  std::uint16_t entity_reference{0U};
  std::uint16_t sound_reference{0U};
  std::array<float, 3U> origin{};
  std::optional<std::uint8_t> pitch{};
  std::size_t encoded_body_bits{0U};

  [[nodiscard]] friend bool operator==(const RuntimeControlSound &,
                                       const RuntimeControlSound &) = default;
};

struct RuntimeControlText final {
  std::size_t text_length{0U};

  [[nodiscard]] friend bool operator==(const RuntimeControlText &,
                                       const RuntimeControlText &) = default;
};

struct RuntimeControlExactFixedBody final {
    std::size_t body_size{0U};
    // Owning E1 projection; fixed-control kind/hash contract remains unchanged.
    std::optional<RuntimeControlSound> sound;

  [[nodiscard]] friend bool
  operator==(const RuntimeControlExactFixedBody &,
             const RuntimeControlExactFixedBody &) = default;
};

struct RuntimeControlVoiceInitialization final {
  std::size_t codec_name_length{0U};
  std::uint8_t quality{0U};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlVoiceInitialization &,
             const RuntimeControlVoiceInitialization &) = default;
};

struct RuntimeControlUserMessage final {
  std::uint8_t identifier{0U};
  std::int8_t declared_size{0};
  std::size_t body_size{0U};
  bool variable_size{false};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlUserMessage &,
             const RuntimeControlUserMessage &) = default;
};

// Generic, owning wire observations only. No .sc execution, game-specific
// effect selection, entity lookup, or retained event base is implied. The
// packet index refers to a packet entity list, NOT an edict/entity number.
struct RuntimeControlScriptedEvent final {
  std::uint16_t event_index{0U};
  std::optional<std::uint16_t> packet_index;
  std::shared_ptr<const DeltaObjectState> arguments;
  // Raw 16-bit wire delay; this framing slice does not schedule callbacks or
  // claim stock timing units.
  std::optional<std::uint16_t> fire_delay_ticks;
  // Exact transmitted delta-field presence, independent of their decoded
  // values. Event vector defaults may come from a packet entity; a genuine
  // explicit zero component must not be mistaken for an omitted vector.
  std::uint64_t argument_field_mask{0U};

  [[nodiscard]] friend bool operator==(const RuntimeControlScriptedEvent& left,
                                       const RuntimeControlScriptedEvent& right) {
    return left.event_index == right.event_index &&
           left.argument_field_mask == right.argument_field_mask &&
           left.packet_index == right.packet_index &&
           left.fire_delay_ticks == right.fire_delay_ticks &&
           ((!left.arguments && !right.arguments) ||
            (left.arguments && right.arguments &&
             left.arguments->decode_profile() == right.arguments->decode_profile() &&
             left.arguments->has_equal_values_as(*right.arguments)));
  }
};

struct RuntimeControlScriptedEvents final {
  bool reliable{false};
  std::vector<RuntimeControlScriptedEvent> entries;
  std::size_t encoded_body_bits{0U};

  [[nodiscard]] friend bool operator==(const RuntimeControlScriptedEvents&,
                                       const RuntimeControlScriptedEvents&) = default;
};

struct RuntimeControlServerTime final {
  float seconds{0.0F};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlServerTime &,
             const RuntimeControlServerTime &) = default;
};

struct RuntimeControlViewEntityReference final {
  std::int16_t wire_entity_reference{0};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlViewEntityReference &,
             const RuntimeControlViewEntityReference &) = default;
};

struct RuntimeControlBspDecal final {
  std::uint8_t temporary_entity_type{13U};
  std::array<std::int16_t, 3U> coordinate_eighths{};
  std::int16_t decal_reference{0};
  std::int16_t entity_reference{0};
  std::optional<std::int16_t> model_reference{};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlBspDecal &,
             const RuntimeControlBspDecal &) = default;
};

// Owning Protocol 48 observation, not a request to render a decal or play a
// ricochet. These byte-indexed formats are distinct from TE_BSPDECAL's shorts.
struct RuntimeControlDecal final {
  std::uint8_t temporary_entity_type{116U};
  std::array<std::int16_t, 3U> coordinate_eighths{};
  // HIGH formats add 256 to the wire byte; valid resolved range is 0..511.
  std::uint16_t decal_reference{0U};
  // Implicit zero for WORLDDECAL/HIGH; otherwise the unmodified wire short.
  std::int16_t entity_reference{0};

  [[nodiscard]] friend bool operator==(const RuntimeControlDecal &,
                                       const RuntimeControlDecal &) = default;
};

struct RuntimeControlUserInfoUpdate final {
  std::uint8_t client_index{0U};
  std::size_t info_string_length{0U};
  std::size_t info_entry_count{0U};
  std::size_t opaque_suffix_size{0U};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlUserInfoUpdate &,
             const RuntimeControlUserInfoUpdate &) = default;
};

struct RuntimeControlLightStyle final {
  std::uint8_t style_index{0U};
  std::size_t pattern_length{0U};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlLightStyle &,
             const RuntimeControlLightStyle &) = default;
};

struct RuntimeControlViewAngles final {
  std::array<std::int16_t, 3U> angle_shorts{};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlViewAngles &,
             const RuntimeControlViewAngles &) = default;
};

struct RuntimeControlSignonControl final {
  std::uint8_t signon_number{0U};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlSignonControl &,
             const RuntimeControlSignonControl &) = default;
};

using RuntimeControlMessageBody =
    std::variant<RuntimeControlNop, RuntimeControlChoke, RuntimeControlSound,
                 RuntimeControlText, RuntimeControlExactFixedBody,
                 RuntimeControlServerTime, RuntimeControlViewEntityReference,
                 RuntimeControlViewAngles, RuntimeControlLightStyle,
                 RuntimeControlUserInfoUpdate, RuntimeControlBspDecal,
                 RuntimeControlSignonControl, RuntimeControlVoiceInitialization,
                 RuntimeControlUserMessage, RuntimeControlDecal,
                 RuntimeControlScriptedEvents>;

struct RuntimeControlEvent final {
  RuntimeControlOpcode opcode{RuntimeControlOpcode::svc_nop};
  RuntimeControlMessageKind kind{RuntimeControlMessageKind::nop};
  RuntimeControlMessageProvenance provenance{};
  RuntimeControlMessageBody body{RuntimeControlNop{}};

  [[nodiscard]] friend bool operator==(const RuntimeControlEvent &,
                                       const RuntimeControlEvent &) = default;
};

struct RuntimeControlServerTimeObservation final {
  float seconds{0.0F};
  RuntimeControlMessageProvenance provenance{};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlServerTimeObservation &,
             const RuntimeControlServerTimeObservation &) = default;
};

struct RuntimeControlViewObservation final {
  // This is only the svc_setview entity reference. It is not local-player,
  // authentication, prediction, or server-log identity.
  std::int16_t wire_entity_reference{0};
  RuntimeControlMessageProvenance provenance{};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlViewObservation &,
             const RuntimeControlViewObservation &) = default;
};

struct RuntimeControlSignonObservation final {
  // This first profile supports the one-byte signon value emitted as 1 by
  // the selected Protocol 48 reference. It is not a renderer-ready claim.
  std::uint8_t signon_number{0U};
  RuntimeControlMessageProvenance provenance{};

  [[nodiscard]] friend bool
  operator==(const RuntimeControlSignonObservation &,
             const RuntimeControlSignonObservation &) = default;
};

class RuntimeControlState final {
public:
  explicit RuntimeControlState(
      std::uint64_t source_generation,
      RuntimeControlCompatibilityProfile profile =
          RuntimeControlCompatibilityProfile::
              public_goldsrc48_runtime_control_v1) noexcept;

  RuntimeControlState(const RuntimeControlState &) = default;
  RuntimeControlState &operator=(const RuntimeControlState &) = default;
  RuntimeControlState(RuntimeControlState &&) noexcept = default;
  RuntimeControlState &operator=(RuntimeControlState &&) noexcept = default;
  ~RuntimeControlState() = default;

  [[nodiscard]] bool
  reset_source_generation(std::uint64_t source_generation) noexcept;

  [[nodiscard]] std::uint64_t source_generation() const noexcept;
  [[nodiscard]] RuntimeControlCompatibilityProfile profile() const noexcept;
  [[nodiscard]] RuntimeControlSpecificationSource
  specification_source() const noexcept;
  [[nodiscard]] RuntimeControlStockVerification
  stock_verification() const noexcept;
  [[nodiscard]] const std::optional<RuntimeControlServerTimeObservation> &
  server_time() const noexcept;
  [[nodiscard]] const std::optional<RuntimeControlViewObservation> &
  view_entity() const noexcept;
  [[nodiscard]] const std::optional<RuntimeControlSignonObservation> &
  signon_control() const noexcept;
  [[nodiscard]] std::size_t committed_payload_count() const noexcept;
  [[nodiscard]] std::size_t committed_message_count() const noexcept;

  [[nodiscard]] friend bool operator==(const RuntimeControlState &,
                                       const RuntimeControlState &) = default;

private:
  friend class RuntimeControlDecoder;

  std::uint64_t source_generation_{0U};
  RuntimeControlCompatibilityProfile profile_{
      RuntimeControlCompatibilityProfile::public_goldsrc48_runtime_control_v1};
  std::optional<RuntimeControlServerTimeObservation> server_time_{};
  std::optional<RuntimeControlViewObservation> view_entity_{};
  std::optional<RuntimeControlSignonObservation> signon_control_{};
  std::size_t committed_payload_count_{0U};
  std::size_t committed_message_count_{0U};
};

struct RuntimeControlDecodeInput final {
  const OwnedServicePayload &payload;
  StockRuntimeSourceCursor initial_cursor{};
  std::uint64_t source_generation{0U};
  std::size_t payload_ordinal{0U};
  std::span<const PostMoveVarsUserMessageDefinition> user_message_definitions{};
  // Borrowed for this decode call only; decoded arguments are independently
  // owned. Missing event_t fails closed only when an arguments delta is sent.
  const DeltaSchemaRegistryState* schemas{nullptr};
  std::optional<double> server_time_seconds{};
};

enum class RuntimeControlDecodeErrorCode : std::uint8_t {
  invalid_configuration,
  invalid_profile,
  payload_not_decompressed,
  wrong_direction,
  payload_too_large,
  invalid_cursor,
  unsupported_alignment,
  source_generation_mismatch,
  truncated_opcode,
  truncated_body,
  invalid_numeric_value,
  user_info_update_failed,
  unsupported_temporary_entity_type,
  unsupported_opcode,
  message_limit_exceeded,
  size_overflow,
  unable_to_retain_output,
  missing_event_schema,
  event_delta_failed,
  scripted_event_limit_exceeded,
};

struct RuntimeControlDecodeError final {
  RuntimeControlDecodeErrorCode code{
      RuntimeControlDecodeErrorCode::invalid_configuration};
  std::optional<StockRuntimeSourceCursor> cursor{};
  std::optional<std::uint8_t> wire_opcode{};
  std::string context{};
  std::optional<StockRuntimeSourceCursor> failure_cursor;
  std::optional<std::uint8_t> registration_id;
  std::string registration_name;
  std::optional<std::size_t> expected_body_size;
  std::optional<std::size_t> actual_body_size;
};

struct RuntimeControlDecodedBatch final {
  std::vector<RuntimeControlEvent> events{};
  StockRuntimeSourceCursor start_cursor{};
  StockRuntimeSourceCursor end_cursor{};
  std::size_t consumed_byte_count{0U};
  std::size_t consumed_bit_count{0U};
  std::uint64_t source_generation{0U};
  std::size_t payload_ordinal{0U};
  RuntimeControlCompatibilityProfile profile{
      RuntimeControlCompatibilityProfile::public_goldsrc48_runtime_control_v1};
  RuntimeControlSpecificationSource specification_source{
      RuntimeControlSpecificationSource::public_protocol_reference};
  RuntimeControlStockVerification stock_verification{
      RuntimeControlStockVerification::
          not_verified_against_stock_runtime_payload};
};

struct RuntimeControlDecodeResult final {
  std::optional<RuntimeControlDecodedBatch> batch{};
  std::optional<RuntimeControlDecodeError> error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return batch.has_value() && !error.has_value();
  }
};

struct RuntimeControlSingleDecodeResult final {
  std::optional<RuntimeControlEvent> event{};
  std::optional<RuntimeControlDecodeError> error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return event.has_value() && !error.has_value();
  }
};

class RuntimeControlDecoder final {
public:
  explicit RuntimeControlDecoder(
      RuntimeControlDecodeLimits limits = {},
      RuntimeControlCompatibilityProfile profile =
          RuntimeControlCompatibilityProfile::
              public_goldsrc48_runtime_control_v1) noexcept;

  [[nodiscard]] bool valid_configuration() const noexcept;
  [[nodiscard]] const RuntimeControlDecodeLimits &limits() const noexcept;
  [[nodiscard]] RuntimeControlCompatibilityProfile profile() const noexcept;

  // Shared one-message primitive used by the mixed packet-entity service
  // dispatcher. It consumes exactly one supported control message and does
  // not mutate state.
  [[nodiscard]] RuntimeControlSingleDecodeResult
  decode_one(const RuntimeControlDecodeInput &input,
             std::size_t message_ordinal) const;

  // Applies an already decoded control event sequence to a staged state.
  // Counter and observation changes are atomic.
  [[nodiscard]] std::optional<RuntimeControlDecodeError>
  apply_events(std::span<const RuntimeControlEvent> events,
               RuntimeControlState &state, bool count_payload = true) const;

  // The input is an already-owned server service-message payload, not a UDP
  // datagram. Publication is atomic for the entire suffix: on any error the
  // state and event output remain unchanged/unpublished.
  [[nodiscard]] RuntimeControlDecodeResult
  decode_and_apply(const RuntimeControlDecodeInput &input,
                   RuntimeControlState &state) const;

private:
  RuntimeControlDecodeLimits limits_{};
  RuntimeControlCompatibilityProfile profile_{
      RuntimeControlCompatibilityProfile::public_goldsrc48_runtime_control_v1};
};

[[nodiscard]] std::string_view
to_string(RuntimeControlCompatibilityProfile profile) noexcept;
[[nodiscard]] std::string_view
to_string(RuntimeControlSpecificationSource source) noexcept;
[[nodiscard]] std::string_view
to_string(RuntimeControlStockVerification verification) noexcept;
[[nodiscard]] std::string_view to_string(RuntimeControlOpcode opcode) noexcept;
[[nodiscard]] std::string_view
to_string(RuntimeControlMessageKind kind) noexcept;
[[nodiscard]] std::string_view
to_string(RuntimeControlDecodeErrorCode code) noexcept;

} // namespace hlclient::goldsrc

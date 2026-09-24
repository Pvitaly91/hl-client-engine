#include <hlclient/goldsrc/client_data_decoder.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>
#include <hlclient/goldsrc/netchan_sequence.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr auto kDeltaProfile =
    DeltaValueCompatibilityProfile::public_goldsrc48_delta_v1;

[[nodiscard]] ClientDataDecodeResult failure(
    const ClientDataDecodeErrorCode code,
    std::string context,
    const ClientDataRecoveryStatus recovery = ClientDataRecoveryStatus::none,
    std::optional<StockRuntimeSourceCursor> cursor = std::nullopt,
    std::optional<std::uint8_t> weapon_index = std::nullopt,
    std::optional<ClientDataWireDeltaBaseTag> tag = std::nullopt,
    std::optional<ClientDataResolvedFrameReference> resolved = std::nullopt,
    std::optional<DeltaValueErrorCode> delta_error = std::nullopt)
{
    return ClientDataDecodeResult{
        std::nullopt,
        ClientDataDecodeError{code, recovery, std::move(cursor), weapon_index,
                              tag, resolved, delta_error,
                              std::move(context)}};
}

[[nodiscard]] std::optional<StockRuntimeSourceCursor> cursor_at(
    const std::size_t bit_offset,
    const std::size_t payload_size) noexcept
{
    return StockRuntimeSourceCursor::create(
        bit_offset / 8U, bit_offset & 7U, payload_size);
}

[[nodiscard]] std::string delta_failure_context(
    const DeltaValueError* error,
    const DeltaSchema& schema,
    const std::string_view fallback)
{
    std::string context = error ? error->context : std::string{fallback};
    if (error && error->field_index) {
        context += ";field-index=" + std::to_string(*error->field_index);
        if (*error->field_index < schema.fields().size()) {
            const auto& field = schema.fields()[*error->field_index];
            context += ";field-name=" + std::string{field.name()};
            context += ";field-type=" +
                std::string{to_string(field.type_flags().base_type())};
            context += ";field-signed=" +
                std::string{field.type_flags().signed_value() ? "true" : "false"};
            context += ";field-bits=" +
                std::to_string(field.significant_bits());
        }
    }
    return context;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] std::uint64_t payload_hash(
    const std::span<const std::byte> bytes) noexcept
{
    std::uint64_t hash = 1'469'598'103'934'665'603ULL;
    for (const auto byte : bytes) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

[[nodiscard]] bool reference_less(
    const ClientDataFrameReference& left,
    const ClientDataFrameReference& right) noexcept
{
    const auto left_sequence =
        NetchanSequence::from_numeric(left.source_transport_sequence());
    const auto right_sequence =
        NetchanSequence::from_numeric(right.source_transport_sequence());
    return left_sequence && right_sequence &&
           compare_sequences(*left_sequence, *right_sequence) ==
               NetchanSequenceComparison::older;
}

[[nodiscard]] bool reference_less_equal(
    const ClientDataFrameReference& left,
    const ClientDataFrameReference& right) noexcept
{
    return left == right || reference_less(left, right);
}

[[nodiscard]] std::optional<ClientDataResolvedFrameReference>
resolve_base_reference(
    const NetchanSequence current,
    const ClientDataWireDeltaBaseTag tag) noexcept
{
    const auto current_low = current.value() & 0xffU;
    const auto distance = (current_low - tag.value) & 0xffU;
    if (distance == 0U ||
        distance > kGoldSrcClientDataMaximumDeltaLookback) {
        return std::nullopt;
    }
    const auto resolved =
        (current.value() + kNetchanSequenceModulus - distance) &
        kNetchanSequenceMask;
    return ClientDataResolvedFrameReference{resolved};
}

[[nodiscard]] bool frame_matches_schemas(
    const ClientDataFrameState& frame,
    const DeltaSchema& client_schema,
    const DeltaSchema& weapon_schema,
    const std::uint64_t generation) noexcept
{
    if (frame.provenance().source_generation != generation ||
        frame.profile() !=
            ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1 ||
        frame.client_data().decode_profile() != kDeltaProfile ||
        !frame.client_data().matches_schema(client_schema) ||
        frame.weapon_slots().size() != kGoldSrcWeaponSlotCount) {
        return false;
    }
    return std::all_of(
        frame.weapon_slots().begin(), frame.weapon_slots().end(),
        [&weapon_schema](const ClientWeaponSlotState& slot) noexcept {
            return slot.object().decode_profile() == kDeltaProfile &&
                   slot.object().matches_schema(weapon_schema);
        });
}

} // namespace

std::optional<ClientDataFrameReference>
ClientDataFrameReference::from_transport_sequence(
    const std::uint32_t value) noexcept
{
    if (!NetchanSequence::from_numeric(value)) {
        return std::nullopt;
    }
    return ClientDataFrameReference{value};
}

ClientDataFrameReference::ClientDataFrameReference(
    const std::uint32_t value) noexcept
    : source_transport_sequence_{value}
{
}

std::uint32_t ClientDataFrameReference::source_transport_sequence()
    const noexcept
{
    return source_transport_sequence_;
}

bool valid_client_data_profile(
    const ClientDataCompatibilityProfile profile) noexcept
{
    return profile ==
           ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1;
}

bool valid_client_data_decode_limits(
    const ClientDataDecodeLimits& limits) noexcept
{
    return valid_goldsrc_delta_value_limits(limits.delta_values) &&
           limits.maximum_payload_bytes > 0U &&
           limits.maximum_payload_bytes <= kMaximumClientDataPayloadBytes &&
           limits.maximum_weapon_records > 0U &&
           limits.maximum_weapon_records <= kGoldSrcWeaponSlotCount &&
           limits.maximum_history_frames > 0U &&
           limits.maximum_history_frames <= kMaximumClientDataHistoryFrames &&
           limits.maximum_frame_value_bytes > 0U &&
           limits.maximum_frame_value_bytes <=
               kMaximumClientDataFrameValueBytes &&
           limits.maximum_history_value_bytes >=
               limits.maximum_frame_value_bytes &&
           limits.maximum_history_value_bytes <=
               kMaximumClientDataHistoryValueBytes;
}

ClientWeaponSlotState::ClientWeaponSlotState(
    const std::uint8_t wire_index,
    std::shared_ptr<const DeltaObjectState> object) noexcept
    : wire_index_{wire_index}, object_{std::move(object)}
{
}

std::uint8_t ClientWeaponSlotState::wire_index() const noexcept
{
    return wire_index_;
}

const DeltaObjectState& ClientWeaponSlotState::object() const noexcept
{
    return *object_;
}

bool ClientWeaponSlotState::shares_object_with(
    const ClientWeaponSlotState& other) const noexcept
{
    return object_ == other.object_;
}

ClientDataFrameState::ClientDataFrameState(
    ClientDataFrameReference reference,
    std::optional<ClientDataFrameReference> base_reference,
    const double server_time_seconds,
    std::shared_ptr<const DeltaObjectState> client_data,
    std::vector<ClientWeaponSlotState> weapon_slots,
    std::vector<std::uint8_t> wire_updated_weapon_indices,
    ClientDataFrameProvenance provenance,
    ClientDataFrameStatistics statistics,
    const ClientDataCompatibilityProfile profile) noexcept
    : reference_{std::move(reference)},
      base_reference_{std::move(base_reference)},
      server_time_seconds_{server_time_seconds},
      client_data_{std::move(client_data)},
      weapon_slots_{std::move(weapon_slots)},
      wire_updated_weapon_indices_{std::move(wire_updated_weapon_indices)},
      provenance_{std::move(provenance)},
      statistics_{statistics},
      profile_{profile}
{
}

const ClientDataFrameReference& ClientDataFrameState::reference() const noexcept
{
    return reference_;
}

const std::optional<ClientDataFrameReference>&
ClientDataFrameState::base_reference() const noexcept
{
    return base_reference_;
}

double ClientDataFrameState::server_time_seconds() const noexcept
{
    return server_time_seconds_;
}

const DeltaObjectState& ClientDataFrameState::client_data() const noexcept
{
    return *client_data_;
}

std::span<const ClientWeaponSlotState>
ClientDataFrameState::weapon_slots() const noexcept
{
    return weapon_slots_;
}

const ClientWeaponSlotState* ClientDataFrameState::find_weapon_slot(
    const std::uint8_t wire_index) const noexcept
{
    if (wire_index >= weapon_slots_.size()) {
        return nullptr;
    }
    return &weapon_slots_[wire_index];
}

std::span<const std::uint8_t>
ClientDataFrameState::wire_updated_weapon_indices() const noexcept
{
    return wire_updated_weapon_indices_;
}

const ClientDataFrameProvenance& ClientDataFrameState::provenance() const noexcept
{
    return provenance_;
}

const ClientDataFrameStatistics& ClientDataFrameState::statistics() const noexcept
{
    return statistics_;
}

ClientDataCompatibilityProfile ClientDataFrameState::profile() const noexcept
{
    return profile_;
}

bool ClientDataFrameState::shares_client_object_with(
    const ClientDataFrameState& other) const noexcept
{
    return client_data_ == other.client_data_;
}

ClientDataHistoryState::ClientDataHistoryState(
    std::vector<std::shared_ptr<const ClientDataFrameState>> frames,
    std::optional<ClientDataFrameReference> evicted_through,
    const std::size_t accounted_value_bytes,
    const std::uint64_t source_generation,
    const ClientDataCompatibilityProfile profile) noexcept
    : frames_{std::move(frames)},
      evicted_through_{std::move(evicted_through)},
      accounted_value_bytes_{accounted_value_bytes},
      source_generation_{source_generation},
      profile_{profile}
{
}

std::span<const std::shared_ptr<const ClientDataFrameState>>
ClientDataHistoryState::frames() const noexcept
{
    return frames_;
}

std::size_t ClientDataHistoryState::frame_count() const noexcept
{
    return frames_.size();
}

const ClientDataFrameState* ClientDataHistoryState::find_exact(
    const ClientDataFrameReference& reference) const noexcept
{
    const auto found = std::find_if(
        frames_.begin(), frames_.end(),
        [&reference](const auto& frame) noexcept {
            return frame && frame->reference() == reference;
        });
    return found == frames_.end() ? nullptr : found->get();
}

ClientDataHistoryReferenceStatus ClientDataHistoryState::classify(
    const ClientDataFrameReference& reference) const noexcept
{
    if (find_exact(reference) != nullptr) {
        return ClientDataHistoryReferenceStatus::retained;
    }
    if (!frames_.empty() &&
        reference_less(frames_.back()->reference(), reference)) {
        return ClientDataHistoryReferenceStatus::future;
    }
    if (evicted_through_ &&
        reference_less_equal(reference, *evicted_through_)) {
        return ClientDataHistoryReferenceStatus::evicted;
    }
    return ClientDataHistoryReferenceStatus::missing;
}

std::optional<ClientDataFrameReference>
ClientDataHistoryState::newest_reference() const noexcept
{
    if (frames_.empty()) {
        return std::nullopt;
    }
    return frames_.back()->reference();
}

std::optional<ClientDataFrameReference>
ClientDataHistoryState::evicted_through() const noexcept
{
    return evicted_through_;
}

std::size_t ClientDataHistoryState::accounted_value_bytes() const noexcept
{
    return accounted_value_bytes_;
}

std::uint64_t ClientDataHistoryState::source_generation() const noexcept
{
    return source_generation_;
}

ClientDataCompatibilityProfile ClientDataHistoryState::profile() const noexcept
{
    return profile_;
}

ClientDataSnapshotState::ClientDataSnapshotState(
    const std::uint64_t source_generation,
    std::shared_ptr<const DeltaSchemaRegistryState> schemas,
    const ClientDataDecodeLimits limits,
    const ClientDataCompatibilityProfile profile)
    : source_generation_{source_generation},
      schemas_{std::move(schemas)},
      limits_{limits},
      profile_{profile}
{
    history_ = std::shared_ptr<const ClientDataHistoryState>{
        new ClientDataHistoryState{
            std::vector<std::shared_ptr<const ClientDataFrameState>>{},
            std::nullopt, 0U, source_generation_, profile_}};
}

bool ClientDataSnapshotState::valid() const noexcept
{
    return source_generation_ != 0U && schemas_ && history_ &&
           valid_client_data_decode_limits(limits_) &&
           valid_client_data_profile(profile_) &&
           history_->source_generation() == source_generation_ &&
           history_->profile() == profile_;
}

bool ClientDataSnapshotState::reset_source_generation(
    const std::uint64_t source_generation,
    std::shared_ptr<const DeltaSchemaRegistryState> schemas)
{
    try {
        ClientDataSnapshotState replacement{
            source_generation, std::move(schemas), limits_, profile_};
        if (!replacement.valid()) {
            return false;
        }
        *this = std::move(replacement);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

std::uint64_t ClientDataSnapshotState::source_generation() const noexcept
{
    return source_generation_;
}

const DeltaSchemaRegistryState& ClientDataSnapshotState::schemas() const noexcept
{
    return *schemas_;
}

const ClientDataHistoryState& ClientDataSnapshotState::history() const noexcept
{
    return *history_;
}

const std::shared_ptr<const ClientDataFrameState>&
ClientDataSnapshotState::current_frame() const noexcept
{
    return current_frame_;
}

const ClientDataDecodeLimits& ClientDataSnapshotState::limits() const noexcept
{
    return limits_;
}

ClientDataCompatibilityProfile ClientDataSnapshotState::profile() const noexcept
{
    return profile_;
}

GoldSrcClientDataDecoder::GoldSrcClientDataDecoder(
    const ClientDataDecodeLimits limits,
    const ClientDataCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool GoldSrcClientDataDecoder::valid_configuration() const noexcept
{
    return valid_client_data_decode_limits(limits_) &&
           valid_client_data_profile(profile_);
}

const ClientDataDecodeLimits& GoldSrcClientDataDecoder::limits() const noexcept
{
    return limits_;
}

ClientDataCompatibilityProfile GoldSrcClientDataDecoder::profile() const noexcept
{
    return profile_;
}

ClientDataDecodeResult GoldSrcClientDataDecoder::decode_one_and_apply(
    const ClientDataDecodeInput& input,
    ClientDataSnapshotState& state) const
try {
    if (!valid_client_data_profile(profile_)) {
        return failure(ClientDataDecodeErrorCode::invalid_profile,
                       "unknown clientdata compatibility profile");
    }
    if (!valid_client_data_decode_limits(limits_) || !state.valid() ||
        state.limits_ != limits_) {
        return failure(ClientDataDecodeErrorCode::invalid_configuration,
                       "clientdata decoder and state limits are invalid or incompatible");
    }
    if (state.profile_ != profile_) {
        return failure(ClientDataDecodeErrorCode::invalid_profile,
                       "clientdata decoder and state profiles differ");
    }
    if (input.receiver_mode != ClientDataReceiverMode::ordinary_game_client) {
        return failure(ClientDataDecodeErrorCode::unsupported_receiver_mode,
                       "proxy/HLTV svc_clientdata has an opcode-only body and is outside the ordinary-client profile",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }
    if (!service_payload_decode_ready(input.payload)) {
        return failure(ClientDataDecodeErrorCode::payload_not_decompressed,
                       "svc_clientdata requires an owning decompressed service payload");
    }
    if (input.payload.direction != NetchanDirection::server_to_client) {
        return failure(ClientDataDecodeErrorCode::wrong_direction,
                       "svc_clientdata accepts only server-to-client payloads");
    }
    if (input.payload.bytes.size() > limits_.maximum_payload_bytes) {
        return failure(ClientDataDecodeErrorCode::payload_too_large,
                       "svc_clientdata owning payload exceeds the configured bound");
    }
    if (!valid_stock_runtime_source_cursor(
            input.start_cursor, input.payload.bytes.size())) {
        return failure(ClientDataDecodeErrorCode::invalid_cursor,
                       "svc_clientdata start cursor is outside the owning payload");
    }
    if (!input.start_cursor.byte_aligned()) {
        return failure(ClientDataDecodeErrorCode::unsupported_alignment,
                       "svc_clientdata opcode must begin on a byte boundary",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }
    if (input.source_generation == 0U ||
        input.source_generation != state.source_generation_) {
        return failure(ClientDataDecodeErrorCode::source_generation_mismatch,
                       "svc_clientdata generation does not match its history",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }
    const auto source_sequence =
        NetchanSequence::from_numeric(input.payload.source_sequence);
    if (!source_sequence) {
        return failure(ClientDataDecodeErrorCode::invalid_source_sequence,
                       "svc_clientdata source sequence is outside the 30-bit transport domain",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }
    const auto bytes = std::span<const std::byte>{input.payload.bytes};
    if (input.start_cursor.byte_offset() >= bytes.size() ||
        std::to_integer<std::uint8_t>(
            bytes[input.start_cursor.byte_offset()]) !=
            kGoldSrcSvcClientDataOpcode) {
        return failure(ClientDataDecodeErrorCode::wrong_opcode,
                       "clientdata decoder requires svc_clientdata at the exact cursor",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }
    if (!input.server_time_seconds ||
        !std::isfinite(*input.server_time_seconds)) {
        return failure(ClientDataDecodeErrorCode::missing_server_time,
                       "svc_clientdata requires staged finite svc_time for this generation",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }

    const auto fingerprint_hash = payload_hash(bytes);
    const auto fingerprint = std::find_if(
        state.fingerprints_.begin(), state.fingerprints_.end(),
        [source_sequence](const auto& item) noexcept {
            return item.sequence == source_sequence->value();
        });
    if (fingerprint != state.fingerprints_.end()) {
        return failure(
            fingerprint->payload_hash == fingerprint_hash
                ? ClientDataDecodeErrorCode::duplicate_source_frame
                : ClientDataDecodeErrorCode::conflicting_source_frame,
            fingerprint->payload_hash == fingerprint_hash
                ? "duplicate svc_clientdata payload for an already committed source frame"
                : "svc_clientdata source frame was replayed with different payload bytes",
            ClientDataRecoveryStatus::none, input.start_cursor);
    }
    if (const auto newest = state.history_->newest_reference()) {
        const auto newest_sequence = NetchanSequence::from_numeric(
            newest->source_transport_sequence());
        if (!newest_sequence ||
            compare_sequences(*source_sequence, *newest_sequence) !=
                NetchanSequenceComparison::newer) {
            return failure(ClientDataDecodeErrorCode::old_source_frame,
                           "svc_clientdata source frame is old or modularly ambiguous",
                           ClientDataRecoveryStatus::none, input.start_cursor);
        }
    }

    const auto* client_schema =
        state.schemas_->find_exact(input.client_schema_name);
    const auto* weapon_schema =
        state.schemas_->find_exact(input.weapon_schema_name);
    if (client_schema == nullptr || weapon_schema == nullptr) {
        return failure(ClientDataDecodeErrorCode::unknown_schema,
                       "clientdata_t or weapon_data_t is absent from the validated registry",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }

    DeltaObjectBuilder default_builder{limits_.delta_values, kDeltaProfile};
    auto default_client_result = default_builder.build_default(*client_schema);
    auto default_weapon_result = default_builder.build_default(*weapon_schema);
    if (!default_client_result || !default_weapon_result) {
        const auto& selected = !default_client_result
                                   ? default_client_result
                                   : default_weapon_result;
        const auto* error = selected.error ? &*selected.error : nullptr;
        return failure(ClientDataDecodeErrorCode::default_state_failed,
                       error ? error->context
                             : "unable to construct schema-defined clientdata defaults",
                       ClientDataRecoveryStatus::none, input.start_cursor,
                       std::nullopt, std::nullopt, std::nullopt,
                       error ? std::optional{error->code} : std::nullopt);
    }

    std::shared_ptr<const DeltaObjectState> default_client;
    std::shared_ptr<const DeltaObjectState> default_weapon;
    try {
        default_client = std::make_shared<const DeltaObjectState>(
            std::move(*default_client_result.state));
        default_weapon = std::make_shared<const DeltaObjectState>(
            std::move(*default_weapon_result.state));
    } catch (const std::bad_alloc&) {
        return failure(ClientDataDecodeErrorCode::unable_to_retain_output,
                       "unable to retain schema-defined clientdata defaults");
    }

    std::size_t bit_cursor =
        (input.start_cursor.byte_offset() + 1U) * 8U;
    auto read_bits = [&](const std::size_t width)
        -> std::optional<std::uint32_t> {
        BitReader reader{bytes, bit_cursor, bytes.size() * 8U - bit_cursor};
        const auto value = reader.read_bits(width);
        if (!value) {
            return std::nullopt;
        }
        bit_cursor = reader.bit_offset();
        return value.value;
    };

    const auto has_base = read_bits(kGoldSrcClientDataBasePresentBits);
    if (!has_base) {
        return failure(ClientDataDecodeErrorCode::truncated_base_flag,
                       "svc_clientdata base-presence flag is truncated",
                       ClientDataRecoveryStatus::none,
                       cursor_at(bit_cursor, bytes.size()));
    }
    std::optional<ClientDataWireDeltaBaseTag> wire_tag;
    std::optional<ClientDataResolvedFrameReference> resolved_base;
    std::optional<ClientDataFrameReference> base_reference;
    const ClientDataFrameState* base_frame = nullptr;
    if (*has_base != 0U) {
        const auto tag = read_bits(kGoldSrcClientDataBaseTagBits);
        if (!tag) {
            return failure(ClientDataDecodeErrorCode::truncated_base_tag,
                           "svc_clientdata explicit base tag is truncated",
                           ClientDataRecoveryStatus::no_base_message_required,
                           cursor_at(bit_cursor, bytes.size()));
        }
        wire_tag = ClientDataWireDeltaBaseTag{
            static_cast<std::uint8_t>(*tag)};
        resolved_base = resolve_base_reference(*source_sequence, *wire_tag);
        if (!resolved_base) {
            return failure(ClientDataDecodeErrorCode::invalid_delta_base_lookback,
                           "svc_clientdata base tag is current or outside the 62-frame window",
                           ClientDataRecoveryStatus::no_base_message_required,
                           input.start_cursor, std::nullopt, wire_tag);
        }
        base_reference = ClientDataFrameReference::from_transport_sequence(
            resolved_base->source_transport_sequence);
        if (!base_reference) {
            return failure(ClientDataDecodeErrorCode::incompatible_delta_base,
                           "resolved clientdata base is outside the transport domain",
                           ClientDataRecoveryStatus::no_base_message_required,
                           input.start_cursor, std::nullopt, wire_tag,
                           resolved_base);
        }
        base_frame = state.history_->find_exact(*base_reference);
        if (base_frame == nullptr) {
            const auto classification = state.history_->classify(*base_reference);
            return failure(
                classification == ClientDataHistoryReferenceStatus::evicted
                    ? ClientDataDecodeErrorCode::evicted_delta_base
                    : ClientDataDecodeErrorCode::missing_delta_base,
                classification == ClientDataHistoryReferenceStatus::evicted
                    ? "resolved clientdata base was evicted from bounded history"
                    : "resolved exact clientdata base is not committed in this generation",
                ClientDataRecoveryStatus::no_base_message_required,
                input.start_cursor, std::nullopt, wire_tag, resolved_base);
        }
        if (!frame_matches_schemas(
                *base_frame, *client_schema, *weapon_schema,
                input.source_generation)) {
            return failure(ClientDataDecodeErrorCode::incompatible_delta_base,
                           "resolved clientdata base has incompatible generation or schemas",
                           ClientDataRecoveryStatus::no_base_message_required,
                           input.start_cursor, std::nullopt, wire_tag,
                           resolved_base);
        }
    }

    const DeltaObjectState* client_base =
        base_frame ? &base_frame->client_data() : default_client.get();
    GoldSrcDeltaValueDecoder delta_decoder{limits_.delta_values, kDeltaProfile};
    const auto decoded_client = delta_decoder.decode_delta(
        *client_schema, client_base,
        DeltaValueDecodeContext{bytes, bit_cursor,
                                bytes.size() * 8U - bit_cursor,
                                std::nullopt, input.server_time_seconds, false});
    if (!decoded_client) {
        return failure(ClientDataDecodeErrorCode::client_delta_failed,
                       delta_failure_context(
                           decoded_client.error ? &*decoded_client.error : nullptr,
                           *client_schema,
                           "clientdata_t delta decode failed"),
                       ClientDataRecoveryStatus::none,
                       cursor_at(decoded_client.error
                                     ? decoded_client.error->bit_offset
                                     : bit_cursor,
                                 bytes.size()),
                       std::nullopt, wire_tag, resolved_base,
                       decoded_client.error
                           ? std::optional{decoded_client.error->code}
                           : std::nullopt);
    }
    bit_cursor = decoded_client.next_bit_offset;
    const bool client_changed =
        !decoded_client.state->has_equal_values_as(*client_base);
    std::shared_ptr<const DeltaObjectState> client_object;
    std::vector<ClientWeaponSlotState> weapon_slots;
    std::vector<std::uint8_t> updated_indices;
    std::array<bool, kGoldSrcWeaponSlotCount> seen{};
    try {
        client_object = client_changed
                            ? std::make_shared<const DeltaObjectState>(
                                  std::move(*decoded_client.state))
                            : (base_frame ? base_frame->client_data_
                                          : default_client);
        weapon_slots.reserve(kGoldSrcWeaponSlotCount);
        updated_indices.reserve(limits_.maximum_weapon_records);
        for (std::size_t index = 0U;
             index < kGoldSrcWeaponSlotCount; ++index) {
            auto object = base_frame
                              ? base_frame->weapon_slots_[index].object_
                              : default_weapon;
            weapon_slots.emplace_back(ClientWeaponSlotState{
                static_cast<std::uint8_t>(index), std::move(object)});
        }
    } catch (const std::bad_alloc&) {
        return failure(ClientDataDecodeErrorCode::unable_to_retain_output,
                       "unable to stage owning clientdata and weapon slots");
    }

    std::optional<std::uint8_t> previous_index;
    std::size_t changed_weapon_slots = 0U;
    std::size_t wire_records = 0U;
    while (true) {
        const auto continuation = read_bits(kGoldSrcWeaponContinuationBits);
        if (!continuation) {
            return failure(ClientDataDecodeErrorCode::truncated_weapon_flag,
                           "svc_clientdata weapon continuation/terminating flag is truncated",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()),
                           std::nullopt, wire_tag, resolved_base);
        }
        if (*continuation == 0U) {
            break;
        }
        if (wire_records >= limits_.maximum_weapon_records) {
            return failure(ClientDataDecodeErrorCode::weapon_record_limit_exceeded,
                           "svc_clientdata weapon record count exceeds the configured bound",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()),
                           std::nullopt, wire_tag, resolved_base);
        }
        const auto index_read = read_bits(kGoldSrcWeaponIndexBits);
        if (!index_read) {
            return failure(ClientDataDecodeErrorCode::truncated_weapon_index,
                           "svc_clientdata weapon index is truncated",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()),
                           std::nullopt, wire_tag, resolved_base);
        }
        const auto index = static_cast<std::uint8_t>(*index_read);
        if (seen[index]) {
            return failure(ClientDataDecodeErrorCode::duplicate_weapon_index,
                           "svc_clientdata repeats a weapon slot index",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()), index,
                           wire_tag, resolved_base);
        }
        if (previous_index && index < *previous_index) {
            return failure(ClientDataDecodeErrorCode::out_of_order_weapon_index,
                           "ordinary GoldSrc writer order requires ascending weapon indices",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()), index,
                           wire_tag, resolved_base);
        }
        seen[index] = true;
        previous_index = index;
        ++wire_records;

        const auto& weapon_base = weapon_slots[index].object();
        const auto decoded_weapon = delta_decoder.decode_delta(
            *weapon_schema, &weapon_base,
            DeltaValueDecodeContext{bytes, bit_cursor,
                                    bytes.size() * 8U - bit_cursor,
                                    std::nullopt, input.server_time_seconds,
                                    false});
        if (!decoded_weapon) {
            return failure(ClientDataDecodeErrorCode::weapon_delta_failed,
                           delta_failure_context(
                               decoded_weapon.error ? &*decoded_weapon.error : nullptr,
                               *weapon_schema,
                               "weapon_data_t delta decode failed"),
                           ClientDataRecoveryStatus::none,
                           cursor_at(decoded_weapon.error
                                         ? decoded_weapon.error->bit_offset
                                         : bit_cursor,
                                     bytes.size()),
                           index, wire_tag, resolved_base,
                           decoded_weapon.error
                               ? std::optional{decoded_weapon.error->code}
                               : std::nullopt);
        }
        bit_cursor = decoded_weapon.next_bit_offset;
        const bool changed =
            !decoded_weapon.state->has_equal_values_as(weapon_base);
        try {
            if (changed) {
                ++changed_weapon_slots;
                if (decoded_weapon.state->has_equal_values_as(*default_weapon)) {
                    weapon_slots[index].object_ = default_weapon;
                } else {
                    weapon_slots[index].object_ =
                        std::make_shared<const DeltaObjectState>(
                            std::move(*decoded_weapon.state));
                }
            }
            updated_indices.push_back(index);
        } catch (const std::bad_alloc&) {
            return failure(ClientDataDecodeErrorCode::unable_to_retain_output,
                           "unable to retain decoded weapon slot",
                           ClientDataRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()), index,
                           wire_tag, resolved_base);
        }
    }

    BitReader padding{bytes, bit_cursor, bytes.size() * 8U - bit_cursor};
    if (padding.align_to_byte_zero_padding() != BitReaderError::none) {
        return failure(ClientDataDecodeErrorCode::malformed_padding,
                       "svc_clientdata terminator has non-zero or truncated byte padding",
                       ClientDataRecoveryStatus::none,
                       cursor_at(bit_cursor, bytes.size()),
                       std::nullopt, wire_tag, resolved_base);
    }
    bit_cursor = padding.bit_offset();
    const auto end_cursor = cursor_at(bit_cursor, bytes.size());
    if (!end_cursor) {
        return failure(ClientDataDecodeErrorCode::size_overflow,
                       "svc_clientdata end cursor cannot be represented");
    }

    std::size_t accounted_value_bytes = client_object->accounted_value_bytes();
    for (const auto& slot : weapon_slots) {
        if (!checked_add(accounted_value_bytes,
                         slot.object().accounted_value_bytes(),
                         accounted_value_bytes)) {
            return failure(ClientDataDecodeErrorCode::size_overflow,
                           "clientdata frame value-byte accounting overflowed",
                           ClientDataRecoveryStatus::none, input.start_cursor);
        }
    }
    if (accounted_value_bytes > limits_.maximum_frame_value_bytes) {
        return failure(ClientDataDecodeErrorCode::frame_value_limit_exceeded,
                       "clientdata frame exceeds the configured value-byte bound",
                       ClientDataRecoveryStatus::none, input.start_cursor);
    }

    const auto reference = ClientDataFrameReference::from_transport_sequence(
        source_sequence->value());
    if (!reference) {
        return failure(ClientDataDecodeErrorCode::invalid_source_sequence,
                       "clientdata source sequence cannot become a frame reference");
    }
    std::shared_ptr<const ClientDataFrameState> frame;
    std::shared_ptr<const ClientDataHistoryState> history;
    std::vector<ClientDataSnapshotState::FrameFingerprint> fingerprints;
    try {
        frame = std::shared_ptr<const ClientDataFrameState>{
            new ClientDataFrameState{
                *reference, base_reference, *input.server_time_seconds,
                std::move(client_object), std::move(weapon_slots),
                std::move(updated_indices),
                ClientDataFrameProvenance{
                    input.source_generation, source_sequence->value(),
                    input.payload_ordinal, bytes.size(), input.start_cursor,
                    *end_cursor},
                ClientDataFrameStatistics{
                    client_changed, wire_records, changed_weapon_slots,
                    kGoldSrcWeaponSlotCount - changed_weapon_slots,
                    kGoldSrcWeaponSlotCount - wire_records,
                    accounted_value_bytes},
                profile_}};

        auto frames = state.history_->frames_;
        auto evicted_through = state.history_->evicted_through_;
        auto history_bytes = state.history_->accounted_value_bytes_;
        const bool must_evict =
            frames.size() == limits_.maximum_history_frames;
        if (must_evict) {
            evicted_through = frames.front()->reference();
            history_bytes -=
                frames.front()->statistics().accounted_value_bytes;
            frames.erase(frames.begin());
        }
        if (!checked_add(history_bytes, accounted_value_bytes, history_bytes) ||
            history_bytes > limits_.maximum_history_value_bytes) {
            return failure(
                ClientDataDecodeErrorCode::history_value_limit_exceeded,
                "clientdata history exceeds its aggregate value-byte bound",
                ClientDataRecoveryStatus::none, input.start_cursor);
        }
        frames.push_back(frame);
        history = std::shared_ptr<const ClientDataHistoryState>{
            new ClientDataHistoryState{
                std::move(frames), evicted_through, history_bytes,
                input.source_generation, profile_}};

        fingerprints = state.fingerprints_;
        if (fingerprints.size() == limits_.maximum_history_frames) {
            fingerprints.erase(fingerprints.begin());
        }
        fingerprints.push_back(ClientDataSnapshotState::FrameFingerprint{
            source_sequence->value(), fingerprint_hash});
    } catch (const std::bad_alloc&) {
        return failure(ClientDataDecodeErrorCode::unable_to_retain_output,
                       "unable to publish owning clientdata frame and history");
    }

    state.history_ = std::move(history);
    state.current_frame_ = frame;
    state.fingerprints_ = std::move(fingerprints);
    return ClientDataDecodeResult{
        ClientDataMessageEvent{wire_tag, resolved_base, std::move(frame),
                               input.start_cursor, *end_cursor,
                               input.message_ordinal,
                               ClientDataSpecificationSource::
                                   public_protocol_reference,
                               ClientDataStockVerification::
                                   not_verified_against_stock_runtime_payload},
        std::nullopt};
} catch (const std::bad_alloc&) {
    return failure(ClientDataDecodeErrorCode::unable_to_retain_output,
                   "unable to allocate while decoding owning clientdata state",
                   ClientDataRecoveryStatus::none, input.start_cursor);
}

std::string_view to_string(
    const ClientDataCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case ClientDataCompatibilityProfile::public_goldsrc48_clientdata_v1:
        return "public_goldsrc48_clientdata_v1";
    }
    return "unknown";
}

std::string_view to_string(
    const ClientDataSpecificationSource source) noexcept
{
    switch (source) {
    case ClientDataSpecificationSource::public_protocol_reference:
        return "public_protocol_reference";
    }
    return "unknown";
}

std::string_view to_string(
    const ClientDataStockVerification verification) noexcept
{
    switch (verification) {
    case ClientDataStockVerification::not_verified_against_stock_runtime_payload:
        return "not_verified_against_stock_runtime_payload";
    }
    return "unknown";
}

std::string_view to_string(const ClientDataReceiverMode mode) noexcept
{
    switch (mode) {
    case ClientDataReceiverMode::ordinary_game_client:
        return "ordinary_game_client";
    case ClientDataReceiverMode::proxy_or_hltv:
        return "proxy_or_hltv";
    }
    return "unknown";
}

std::string_view to_string(const ClientDataRecoveryStatus status) noexcept
{
    switch (status) {
    case ClientDataRecoveryStatus::none: return "none";
    case ClientDataRecoveryStatus::no_base_message_required:
        return "no_base_message_required";
    }
    return "unknown";
}

std::string_view to_string(const ClientDataDecodeErrorCode code) noexcept
{
    switch (code) {
    case ClientDataDecodeErrorCode::invalid_configuration: return "invalid_configuration";
    case ClientDataDecodeErrorCode::invalid_profile: return "invalid_profile";
    case ClientDataDecodeErrorCode::unsupported_receiver_mode: return "unsupported_receiver_mode";
    case ClientDataDecodeErrorCode::payload_not_decompressed: return "payload_not_decompressed";
    case ClientDataDecodeErrorCode::wrong_direction: return "wrong_direction";
    case ClientDataDecodeErrorCode::payload_too_large: return "payload_too_large";
    case ClientDataDecodeErrorCode::invalid_cursor: return "invalid_cursor";
    case ClientDataDecodeErrorCode::unsupported_alignment: return "unsupported_alignment";
    case ClientDataDecodeErrorCode::source_generation_mismatch: return "source_generation_mismatch";
    case ClientDataDecodeErrorCode::invalid_source_sequence: return "invalid_source_sequence";
    case ClientDataDecodeErrorCode::wrong_opcode: return "wrong_opcode";
    case ClientDataDecodeErrorCode::truncated_base_flag: return "truncated_base_flag";
    case ClientDataDecodeErrorCode::truncated_base_tag: return "truncated_base_tag";
    case ClientDataDecodeErrorCode::invalid_delta_base_lookback: return "invalid_delta_base_lookback";
    case ClientDataDecodeErrorCode::missing_delta_base: return "missing_delta_base";
    case ClientDataDecodeErrorCode::evicted_delta_base: return "evicted_delta_base";
    case ClientDataDecodeErrorCode::incompatible_delta_base: return "incompatible_delta_base";
    case ClientDataDecodeErrorCode::missing_server_time: return "missing_server_time";
    case ClientDataDecodeErrorCode::unknown_schema: return "unknown_schema";
    case ClientDataDecodeErrorCode::default_state_failed: return "default_state_failed";
    case ClientDataDecodeErrorCode::client_delta_failed: return "client_delta_failed";
    case ClientDataDecodeErrorCode::truncated_weapon_flag: return "truncated_weapon_flag";
    case ClientDataDecodeErrorCode::truncated_weapon_index: return "truncated_weapon_index";
    case ClientDataDecodeErrorCode::duplicate_weapon_index: return "duplicate_weapon_index";
    case ClientDataDecodeErrorCode::out_of_order_weapon_index: return "out_of_order_weapon_index";
    case ClientDataDecodeErrorCode::weapon_record_limit_exceeded: return "weapon_record_limit_exceeded";
    case ClientDataDecodeErrorCode::weapon_delta_failed: return "weapon_delta_failed";
    case ClientDataDecodeErrorCode::malformed_padding: return "malformed_padding";
    case ClientDataDecodeErrorCode::frame_value_limit_exceeded: return "frame_value_limit_exceeded";
    case ClientDataDecodeErrorCode::history_value_limit_exceeded: return "history_value_limit_exceeded";
    case ClientDataDecodeErrorCode::duplicate_source_frame: return "duplicate_source_frame";
    case ClientDataDecodeErrorCode::conflicting_source_frame: return "conflicting_source_frame";
    case ClientDataDecodeErrorCode::old_source_frame: return "old_source_frame";
    case ClientDataDecodeErrorCode::history_publish_failed: return "history_publish_failed";
    case ClientDataDecodeErrorCode::size_overflow: return "size_overflow";
    case ClientDataDecodeErrorCode::unable_to_retain_output: return "unable_to_retain_output";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

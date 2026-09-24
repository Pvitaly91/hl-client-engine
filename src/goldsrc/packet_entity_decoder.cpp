#include <hlclient/goldsrc/packet_entity_decoder.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr auto kSnapshotProfile =
    EntitySnapshotCompatibilityProfile::public_goldsrc48_packet_entities_v1;
constexpr auto kDeltaProfile =
    DeltaValueCompatibilityProfile::public_goldsrc48_entity_delta_v1;

[[nodiscard]] PacketEntityDecodeResult failure(
    const PacketEntityDecodeErrorCode code,
    std::string context,
    const PacketEntityRecoveryStatus recovery = PacketEntityRecoveryStatus::none,
    std::optional<StockRuntimeSourceCursor> cursor = std::nullopt,
    std::optional<std::uint8_t> opcode = std::nullopt,
    std::optional<std::uint32_t> entity = std::nullopt,
    std::optional<PacketEntityWireDeltaBaseTag> tag = std::nullopt,
    std::optional<PacketEntityResolvedFrameReference> resolved = std::nullopt,
    std::optional<DeltaValueErrorCode> delta_error = std::nullopt,
    std::optional<ClientDataDecodeErrorCode> clientdata_error = std::nullopt)
{
    return PacketEntityDecodeResult{
        std::nullopt,
        PacketEntityDecodeError{code, recovery, std::move(cursor), opcode,
                                entity, tag, resolved, delta_error,
                                clientdata_error,
                                std::move(context)}};
}

[[nodiscard]] std::optional<StockRuntimeSourceCursor> cursor_at(
    const std::size_t bit_offset,
    const std::size_t payload_size) noexcept
{
    return StockRuntimeSourceCursor::create(
        bit_offset / 8U, bit_offset & 7U, payload_size);
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

[[nodiscard]] bool values_equal(
    const DeltaObjectState& left,
    const DeltaObjectState& right) noexcept
{
    return left.has_equal_values_as(right);
}

[[nodiscard]] std::optional<std::string_view> schema_name_for(
    const PacketEntityDecodeInput& input,
    const EntitySchemaCategory category) noexcept
{
    switch (category) {
    case EntitySchemaCategory::ordinary_entity:
        return input.ordinary_schema_name;
    case EntitySchemaCategory::player_entity:
        return input.player_schema_name;
    case EntitySchemaCategory::custom_entity:
        return input.custom_schema_name;
    case EntitySchemaCategory::alternate_explicit_schema:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] EntitySchemaCategory schema_category_for(
    const bool custom,
    const std::uint32_t entity_number,
    const std::uint32_t max_clients) noexcept
{
    if (custom) {
        return EntitySchemaCategory::custom_entity;
    }
    if (entity_number >= 1U && entity_number <= max_clients) {
        return EntitySchemaCategory::player_entity;
    }
    return EntitySchemaCategory::ordinary_entity;
}

[[nodiscard]] std::optional<std::size_t> instanced_baseline_count(
    const EntityBaselineRegistryState& baselines) noexcept
{
    std::size_t count = 0U;
    for (const auto& baseline : baselines.baselines()) {
        if (baseline.key().kind() == EntityBaselineKeyKind::alternate_slot) {
            ++count;
        }
    }
    if (count > 63U) {
        return std::nullopt;
    }
    for (std::size_t slot = 0U; slot < count; ++slot) {
        if (baselines.find_exact(EntityBaselineKey::for_alternate_slot(
                static_cast<std::uint32_t>(slot))) == nullptr) {
            return std::nullopt;
        }
    }
    return count;
}

[[nodiscard]] std::optional<PacketEntityResolvedFrameReference>
resolve_base_reference(const NetchanSequence current,
                       const PacketEntityWireDeltaBaseTag tag) noexcept
{
    const auto distance =
        ((current.value() & 0xffU) - static_cast<std::uint32_t>(tag.value)) &
        0xffU;
    if (distance == 0U ||
        distance > kGoldSrcPacketEntityMaximumDeltaLookback) {
        return std::nullopt;
    }
    return PacketEntityResolvedFrameReference{
        (current.value() - distance) & kNetchanSequenceMask};
}

[[nodiscard]] std::optional<std::uint16_t> read_u16_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U]))
         << 8U));
}

[[nodiscard]] bool same_snapshot_limits(const EntitySnapshotLimits& left,
                                        const EntitySnapshotLimits& right) noexcept
{
    return left.maximum_baselines == right.maximum_baselines &&
           left.maximum_entities_per_snapshot ==
               right.maximum_entities_per_snapshot &&
           left.maximum_entity_number == right.maximum_entity_number &&
           left.maximum_fields_per_entity == right.maximum_fields_per_entity &&
           left.maximum_changed_fields_per_entity ==
               right.maximum_changed_fields_per_entity &&
           left.maximum_snapshot_history == right.maximum_snapshot_history &&
           left.maximum_snapshot_total_value_bytes ==
               right.maximum_snapshot_total_value_bytes &&
           left.maximum_source_payload_bytes ==
               right.maximum_source_payload_bytes;
}

} // namespace

bool valid_packet_entity_profile(
    const PacketEntityCompatibilityProfile profile) noexcept
{
    return profile == PacketEntityCompatibilityProfile::
                          public_goldsrc48_packet_entities_v1;
}

bool valid_packet_entity_decode_limits(
    const PacketEntityDecodeLimits& limits) noexcept
{
    return valid_entity_snapshot_limits(limits.snapshots) &&
           valid_runtime_control_decode_limits(limits.controls) &&
           valid_client_data_decode_limits(limits.client_data) &&
           limits.maximum_messages_per_payload != 0U &&
           limits.maximum_messages_per_payload <=
               kMaximumPacketEntityMessagesPerPayload &&
           limits.maximum_wire_records != 0U &&
           limits.maximum_wire_records <= kMaximumPacketEntityWireRecords;
}

PacketEntitySnapshotState::PacketEntitySnapshotState(
    const std::uint64_t source_generation,
    const std::uint32_t max_clients,
    std::shared_ptr<const DeltaSchemaRegistryState> schemas,
    std::shared_ptr<const EntityBaselineRegistryState> baselines,
    const EntitySnapshotLimits limits,
    const ClientDataDecodeLimits client_data_limits)
    : source_generation_{source_generation},
      max_clients_{max_clients},
      limits_{limits},
      control_state_{source_generation},
      client_data_state_{source_generation, schemas, client_data_limits},
      schemas_{std::move(schemas)},
      baselines_{std::move(baselines)}
{
    const auto published = EntitySnapshotHistoryBuilder{
        limits_, kSnapshotProfile}.publish();
    if (published) {
        history_ = std::make_shared<const EntitySnapshotHistoryState>(
            std::move(*published.state));
    }
}

bool PacketEntitySnapshotState::valid() const noexcept
{
    if (source_generation_ == 0U || max_clients_ == 0U ||
        max_clients_ > 255U || !valid_entity_snapshot_limits(limits_) ||
        !schemas_ || !baselines_ || !history_ ||
        baselines_->compatibility_profile() !=
            EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1 ||
        history_->compatibility_profile() != kSnapshotProfile ||
        control_state_.source_generation() != source_generation_ ||
        !client_data_state_.valid() ||
        client_data_state_.source_generation() != source_generation_) {
        return false;
    }
    for (const auto& baseline : baselines_->baselines()) {
        if (baseline.source_geometry().source_generation != source_generation_) {
            return false;
        }
    }
    return instanced_baseline_count(*baselines_).has_value();
}

bool PacketEntitySnapshotState::reset_source_generation(
    const std::uint64_t source_generation,
    const std::uint32_t max_clients,
    std::shared_ptr<const DeltaSchemaRegistryState> schemas,
    std::shared_ptr<const EntityBaselineRegistryState> baselines)
{
    try {
        PacketEntitySnapshotState candidate{
            source_generation, max_clients, std::move(schemas),
            std::move(baselines), limits_, client_data_state_.limits()};
        if (!candidate.valid()) {
            return false;
        }
        *this = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

std::uint64_t PacketEntitySnapshotState::source_generation() const noexcept
{
    return source_generation_;
}

std::uint32_t PacketEntitySnapshotState::max_clients() const noexcept
{
    return max_clients_;
}

const RuntimeControlState& PacketEntitySnapshotState::control_state()
    const noexcept
{
    return control_state_;
}

const EntityBaselineRegistryState& PacketEntitySnapshotState::baselines()
    const noexcept
{
    return *baselines_;
}

const EntitySnapshotHistoryState& PacketEntitySnapshotState::history()
    const noexcept
{
    return *history_;
}

const std::shared_ptr<const EntitySnapshotState>&
PacketEntitySnapshotState::current_snapshot() const noexcept
{
    return current_snapshot_;
}

const ClientDataSnapshotState& PacketEntitySnapshotState::client_data_state()
    const noexcept
{
    return client_data_state_;
}

GoldSrcPacketEntityDecoder::GoldSrcPacketEntityDecoder(
    const PacketEntityDecodeLimits limits,
    const PacketEntityCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool GoldSrcPacketEntityDecoder::valid_configuration() const noexcept
{
    return valid_packet_entity_profile(profile_) &&
           valid_packet_entity_decode_limits(limits_);
}

const PacketEntityDecodeLimits& GoldSrcPacketEntityDecoder::limits()
    const noexcept
{
    return limits_;
}

PacketEntityCompatibilityProfile GoldSrcPacketEntityDecoder::profile()
    const noexcept
{
    return profile_;
}

PacketEntityDecodeResult GoldSrcPacketEntityDecoder::decode_and_apply(
    const PacketEntityDecodeInput& input,
    PacketEntitySnapshotState& state) const
{
    if (!valid_packet_entity_profile(profile_)) {
        return failure(PacketEntityDecodeErrorCode::invalid_profile,
                       "unknown packet-entity compatibility profile");
    }
    if (!valid_packet_entity_decode_limits(limits_) || !state.valid() ||
        !same_snapshot_limits(state.limits_, limits_.snapshots) ||
        state.client_data_state_.limits() != limits_.client_data) {
        return failure(PacketEntityDecodeErrorCode::invalid_configuration,
                       "packet-entity decoder or state limits are invalid");
    }
    if (!service_payload_decode_ready(input.payload)) {
        return failure(PacketEntityDecodeErrorCode::payload_not_decompressed,
                       "packet entities require an owning decompressed service payload");
    }
    if (input.payload.direction != NetchanDirection::server_to_client) {
        return failure(PacketEntityDecodeErrorCode::wrong_direction,
                       "packet entities accept only server-to-client payloads");
    }
    if (input.payload.bytes.size() > limits_.controls.maximum_payload_bytes ||
        input.payload.bytes.size() > limits_.snapshots.maximum_source_payload_bytes ||
        input.payload.bytes.size() > limits_.client_data.maximum_payload_bytes) {
        return failure(PacketEntityDecodeErrorCode::payload_too_large,
                       "service payload exceeds the configured packet-entity bound");
    }
    if (!valid_stock_runtime_source_cursor(input.initial_cursor,
                                           input.payload.bytes.size())) {
        return failure(PacketEntityDecodeErrorCode::invalid_cursor,
                       "initial cursor is outside the owning service payload");
    }
    if (!input.initial_cursor.byte_aligned()) {
        return failure(PacketEntityDecodeErrorCode::unsupported_alignment,
                       "service message opcodes must begin on a byte boundary",
                       PacketEntityRecoveryStatus::none, input.initial_cursor);
    }
    if (input.source_generation == 0U ||
        input.source_generation != state.source_generation_) {
        return failure(PacketEntityDecodeErrorCode::source_generation_mismatch,
                       "payload generation does not match packet-entity state",
                       PacketEntityRecoveryStatus::none, input.initial_cursor);
    }
    const auto source_sequence =
        NetchanSequence::from_numeric(input.payload.source_sequence);
    if (!source_sequence) {
        return failure(PacketEntityDecodeErrorCode::invalid_source_sequence,
                       "source transport sequence is outside the 30-bit netchan domain",
                       PacketEntityRecoveryStatus::none, input.initial_cursor);
    }
    if (input.initial_cursor.byte_offset() == input.payload.bytes.size()) {
        return failure(PacketEntityDecodeErrorCode::truncated_opcode,
                       "service message opcode is missing",
                       PacketEntityRecoveryStatus::none, input.initial_cursor);
    }

    std::optional<PacketEntitySnapshotState> staged_state;
    std::vector<PacketEntityStreamEvent> events;
    std::vector<RuntimeControlEvent> control_events;
    try {
        staged_state.emplace(state);
        events.reserve((std::min)(
            input.payload.bytes.size() - input.initial_cursor.byte_offset(),
            limits_.maximum_messages_per_payload));
        control_events.reserve(events.capacity());
    } catch (const std::bad_alloc&) {
        return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                       "unable to reserve decoded service events");
    }
    auto& next = *staged_state;

    std::optional<double> staged_server_time;
    if (state.control_state_.server_time()) {
        staged_server_time = state.control_state_.server_time()->seconds;
    }
    auto current = input.initial_cursor;
    const auto bytes = std::span<const std::byte>{input.payload.bytes};
    const auto hash = payload_hash(bytes);
    RuntimeControlDecoder control_decoder{limits_.controls};

    while (current.byte_offset() < bytes.size()) {
        if (events.size() >= limits_.maximum_messages_per_payload) {
            return failure(PacketEntityDecodeErrorCode::message_limit_exceeded,
                           "service message count exceeds the configured bound",
                           PacketEntityRecoveryStatus::none, current);
        }
        const auto message_start = current;
        const auto opcode = std::to_integer<std::uint8_t>(
            bytes[current.byte_offset()]);

        if (opcode == kGoldSrcSvcClientDataOpcode) {
            GoldSrcClientDataDecoder client_decoder{limits_.client_data};
            const auto decoded = client_decoder.decode_one_and_apply(
                ClientDataDecodeInput{
                    input.payload,
                    current,
                    input.source_generation,
                    input.payload_ordinal,
                    events.size(),
                    staged_server_time,
                    input.client_receiver_mode,
                    input.client_schema_name,
                    input.weapon_schema_name},
                next.client_data_state_);
            if (!decoded) {
                const auto error = decoded.error;
                return failure(
                    PacketEntityDecodeErrorCode::clientdata_failed,
                    error ? error->context : "clientdata decode failed",
                    error && error->recovery ==
                                 ClientDataRecoveryStatus::no_base_message_required
                        ? PacketEntityRecoveryStatus::clientdata_no_base_required
                        : PacketEntityRecoveryStatus::none,
                    error ? error->cursor : std::optional{current},
                    opcode,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    error ? error->delta_error : std::nullopt,
                    error ? std::optional{error->code} : std::nullopt);
            }
            try {
                events.emplace_back(*decoded.event);
            } catch (const std::bad_alloc&) {
                return failure(
                    PacketEntityDecodeErrorCode::unable_to_retain_output,
                    "unable to retain decoded clientdata event",
                    PacketEntityRecoveryStatus::none, current, opcode);
            }
            current = decoded.event->end_cursor;
            continue;
        }

        if (opcode != kGoldSrcSvcPacketEntitiesOpcode &&
            opcode != kGoldSrcSvcDeltaPacketEntitiesOpcode) {
            const auto decoded = control_decoder.decode_one(
                RuntimeControlDecodeInput{input.payload, current,
                                          input.source_generation,
                                          input.payload_ordinal,
                                          input.user_message_definitions},
                events.size());
            if (!decoded) {
                const auto error = decoded.error;
                return failure(
                    error && error->code == RuntimeControlDecodeErrorCode::unsupported_opcode
                        ? PacketEntityDecodeErrorCode::unsupported_opcode
                        : PacketEntityDecodeErrorCode::runtime_control_failed,
                    error ? error->context : "runtime control decode failed",
                    PacketEntityRecoveryStatus::none,
                    error ? error->cursor : std::optional{current},
                    error ? error->wire_opcode : std::optional<std::uint8_t>{opcode});
            }
            if (decoded.event->kind == RuntimeControlMessageKind::server_time) {
                staged_server_time =
                    std::get<RuntimeControlServerTime>(decoded.event->body).seconds;
            }
            try {
                control_events.push_back(*decoded.event);
                events.emplace_back(*decoded.event);
            } catch (const std::bad_alloc&) {
                return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                               "unable to retain decoded control event",
                               PacketEntityRecoveryStatus::none, current, opcode);
            }
            current = decoded.event->provenance.end_cursor;
            continue;
        }

        const bool delta_message =
            opcode == kGoldSrcSvcDeltaPacketEntitiesOpcode;
        const auto existing = std::find_if(
            next.fingerprints_.begin(), next.fingerprints_.end(),
            [source_sequence](const PacketEntitySnapshotState::FrameFingerprint& item) {
                return item.sequence == source_sequence->value();
            });
        if (existing != next.fingerprints_.end()) {
            return failure(
                existing->payload_hash == hash
                    ? PacketEntityDecodeErrorCode::duplicate_source_frame
                    : PacketEntityDecodeErrorCode::conflicting_source_frame,
                existing->payload_hash == hash
                    ? "duplicate packet-entity payload for an already committed transport frame"
                    : "transport frame was replayed with different service payload bytes",
                PacketEntityRecoveryStatus::none, current, opcode);
        }
        if (const auto newest = next.history_->newest_reference()) {
            const auto newest_sequence =
                NetchanSequence::from_numeric(newest->value());
            const auto comparison = newest_sequence
                                        ? compare_sequences(*source_sequence,
                                                            *newest_sequence)
                                        : NetchanSequenceComparison::half_range_ambiguous;
            if (comparison != NetchanSequenceComparison::newer) {
                return failure(PacketEntityDecodeErrorCode::old_source_frame,
                               "packet-entity source frame is old or modularly ambiguous",
                               PacketEntityRecoveryStatus::none, current, opcode);
            }
        }

        std::size_t header_byte = current.byte_offset() + 1U;
        const auto wire_count = read_u16_le(bytes, header_byte);
        if (!wire_count) {
            return failure(PacketEntityDecodeErrorCode::truncated_header,
                           "packet-entity message is missing its 16-bit final entity count",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        if (*wire_count > limits_.snapshots.maximum_entities_per_snapshot) {
            return failure(PacketEntityDecodeErrorCode::entity_limit_exceeded,
                           "16-bit packet-entity count exceeds the configured entity limit",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        header_byte += 2U;
        std::optional<PacketEntityWireDeltaBaseTag> wire_tag;
        std::optional<PacketEntityResolvedFrameReference> resolved_base;
        const EntitySnapshotState* base_snapshot = nullptr;
        std::optional<EntitySnapshotReference> base_reference;
        if (delta_message) {
            if (header_byte >= bytes.size()) {
                return failure(PacketEntityDecodeErrorCode::truncated_header,
                               "delta packet entities is missing its 8-bit base tag",
                               PacketEntityRecoveryStatus::full_snapshot_required,
                               current, opcode);
            }
            wire_tag = PacketEntityWireDeltaBaseTag{
                std::to_integer<std::uint8_t>(bytes[header_byte++])};
            resolved_base = resolve_base_reference(*source_sequence, *wire_tag);
            if (!resolved_base) {
                return failure(PacketEntityDecodeErrorCode::invalid_delta_base_lookback,
                               "delta base tag is current or outside the 62-frame GoldSrc window",
                               PacketEntityRecoveryStatus::full_snapshot_required,
                               current, opcode, std::nullopt, wire_tag);
            }
            const auto reference =
                EntitySnapshotReference::goldsrc_transport_sequence(
                    resolved_base->source_transport_sequence);
            if (!reference) {
                return failure(PacketEntityDecodeErrorCode::incompatible_delta_base,
                               "resolved delta base is outside the netchan sequence domain",
                               PacketEntityRecoveryStatus::full_snapshot_required,
                               current, opcode, std::nullopt, wire_tag,
                               resolved_base);
            }
            base_reference.emplace(*reference);
            base_snapshot = next.history_->find_exact(*base_reference);
            if (base_snapshot == nullptr) {
                const auto classification = next.history_->classify(*base_reference);
                return failure(
                    classification == EntitySnapshotHistoryReferenceStatus::evicted
                        ? PacketEntityDecodeErrorCode::evicted_delta_base
                        : PacketEntityDecodeErrorCode::missing_delta_base,
                    classification == EntitySnapshotHistoryReferenceStatus::evicted
                        ? "resolved delta base was evicted from bounded history"
                        : "resolved exact delta base is not committed in this generation",
                    PacketEntityRecoveryStatus::full_snapshot_required,
                    current, opcode, std::nullopt, wire_tag, resolved_base);
            }
            if (base_snapshot->source_geometry().source_generation !=
                input.source_generation) {
                return failure(PacketEntityDecodeErrorCode::incompatible_delta_base,
                               "resolved delta base belongs to another generation",
                               PacketEntityRecoveryStatus::full_snapshot_required,
                               current, opcode, std::nullopt, wire_tag,
                               resolved_base);
            }
        }
        if (!staged_server_time.has_value() ||
            !std::isfinite(*staged_server_time)) {
            return failure(PacketEntityDecodeErrorCode::missing_server_time,
                           "packet entities require staged svc_time for their frame",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        const auto snapshot_time =
            EntityServerTime::public_goldsrc_seconds(*staged_server_time);
        if (!snapshot_time) {
            return failure(PacketEntityDecodeErrorCode::missing_server_time,
                           "packet-entity server time is not finite",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }

        const auto instance_count = instanced_baseline_count(*next.baselines_);
        if (!instance_count) {
            return failure(PacketEntityDecodeErrorCode::invalid_configuration,
                           "instanced baseline slots are not contiguous or exceed six bits",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        std::size_t bit_cursor = header_byte * 8U;
        std::uint32_t previous_number = 0U;
        std::vector<EntitySnapshotEntityState> decoded_updates;
        std::vector<std::uint32_t> removals;
        std::size_t wire_records = 0U;
        std::size_t changed_count = 0U;
        std::size_t added_count = 0U;
        GoldSrcDeltaValueDecoder delta_decoder{{}, kDeltaProfile};

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

        while (true) {
            BitReader terminator{bytes, bit_cursor,
                                 bytes.size() * 8U - bit_cursor};
            const auto end_marker =
                terminator.read_bits(kGoldSrcPacketEntityTerminatorBits);
            if (!end_marker) {
                return failure(PacketEntityDecodeErrorCode::truncated_terminator,
                               "packet-entity stream lacks a complete 16-bit terminator",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(bit_cursor, bytes.size()), opcode);
            }
            if (end_marker.value == kGoldSrcPacketEntityTerminator) {
                bit_cursor = terminator.bit_offset();
                break;
            }
            if (++wire_records > limits_.maximum_wire_records) {
                return failure(PacketEntityDecodeErrorCode::wire_record_limit_exceeded,
                               "packet-entity wire record count exceeds the configured bound",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(bit_cursor, bytes.size()), opcode);
            }

            const auto record_start = bit_cursor;
            bool remove = false;
            std::optional<std::uint32_t> number;
            if (!delta_message) {
                const auto sequential = read_bits(1U);
                if (!sequential) {
                    return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                   "full entity sequential-number flag is truncated",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode);
                }
                if (*sequential != 0U) {
                    if (previous_number ==
                        (std::numeric_limits<std::uint32_t>::max)()) {
                        return failure(PacketEntityDecodeErrorCode::invalid_entity_number,
                                       "full entity number overflowed",
                                       PacketEntityRecoveryStatus::none,
                                       cursor_at(record_start, bytes.size()), opcode);
                    }
                    number = previous_number + 1U;
                } else {
                    const auto absolute = read_bits(1U);
                    if (!absolute) {
                        return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                       "full entity number branch is truncated",
                                       PacketEntityRecoveryStatus::none,
                                       cursor_at(record_start, bytes.size()), opcode);
                    }
                    if (*absolute != 0U) {
                        number = read_bits(kGoldSrcPacketEntityNumberBits);
                    } else {
                        const auto difference =
                            read_bits(kGoldSrcPacketEntityRelativeNumberBits);
                        if (difference &&
                            *difference <=
                                (std::numeric_limits<std::uint32_t>::max)() -
                                    previous_number) {
                            number = previous_number + *difference;
                        }
                    }
                }
            } else {
                const auto remove_bit = read_bits(1U);
                const auto absolute = read_bits(1U);
                if (!remove_bit || !absolute) {
                    return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                   "delta entity remove/number flags are truncated",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode);
                }
                remove = *remove_bit != 0U;
                if (*absolute != 0U) {
                    number = read_bits(kGoldSrcPacketEntityNumberBits);
                } else {
                    const auto difference =
                        read_bits(kGoldSrcPacketEntityRelativeNumberBits);
                    if (difference &&
                        *difference <=
                            (std::numeric_limits<std::uint32_t>::max)() -
                                previous_number) {
                        number = previous_number + *difference;
                    }
                }
            }
            if (!number) {
                return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                               "packet entity number bits are truncated",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode);
            }
            if (*number == 0U ||
                *number >= (1U << kGoldSrcPacketEntityNumberBits) ||
                *number > limits_.snapshots.maximum_entity_number) {
                return failure(PacketEntityDecodeErrorCode::invalid_entity_number,
                               "packet entity number is outside the supported 11-bit nonzero domain",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            if (*number == previous_number) {
                return failure(PacketEntityDecodeErrorCode::duplicate_entity_record,
                               "packet entity record repeats the previous number",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            if (*number < previous_number) {
                return failure(PacketEntityDecodeErrorCode::out_of_order_entity_record,
                               "packet entity records are not strictly ascending",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            previous_number = *number;

            if (remove) {
                if (base_snapshot == nullptr ||
                    base_snapshot->find_exact(*number) == nullptr) {
                    return failure(PacketEntityDecodeErrorCode::remove_nonexistent_entity,
                                   "explicit removal names an entity absent from the exact base",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                try {
                    removals.push_back(*number);
                } catch (const std::bad_alloc&) {
                    return failure(
                        PacketEntityDecodeErrorCode::unable_to_retain_output,
                        "unable to retain explicit entity removal",
                        PacketEntityRecoveryStatus::none,
                        cursor_at(record_start, bytes.size()), opcode, *number);
                }
                continue;
            }

            const auto custom = read_bits(1U);
            if (!custom) {
                return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                               "packet entity custom-schema bit is truncated",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            bool instanced = false;
            std::uint32_t instance_slot = 0U;
            if (*instance_count != 0U) {
                const auto instanced_bit = read_bits(1U);
                if (!instanced_bit) {
                    return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                   "packet entity instanced-baseline flag is truncated",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                instanced = *instanced_bit != 0U;
                if (instanced) {
                    const auto slot = read_bits(6U);
                    if (!slot) {
                        return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                       "packet entity instanced-baseline slot is truncated",
                                       PacketEntityRecoveryStatus::none,
                                       cursor_at(record_start, bytes.size()), opcode,
                                       *number);
                    }
                    instance_slot = *slot;
                }
            }
            std::uint32_t intra_offset = 0U;
            if (!delta_message && !instanced) {
                const auto has_offset = read_bits(1U);
                if (!has_offset) {
                    return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                   "full entity intra-message baseline flag is truncated",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                if (*has_offset != 0U) {
                    const auto offset = read_bits(6U);
                    if (!offset) {
                        return failure(PacketEntityDecodeErrorCode::truncated_entity_header,
                                       "full entity intra-message baseline offset is truncated",
                                       PacketEntityRecoveryStatus::none,
                                       cursor_at(record_start, bytes.size()), opcode,
                                       *number);
                    }
                    intra_offset = *offset;
                }
            }

            const auto category = schema_category_for(
                *custom != 0U, *number, next.max_clients_);
            const auto schema_name = schema_name_for(input, category);
            const auto* selected_schema = schema_name
                                              ? next.schemas_->find_exact(*schema_name)
                                              : nullptr;
            if (selected_schema == nullptr) {
                return failure(PacketEntityDecodeErrorCode::unknown_schema,
                               "selected packet-entity delta schema is absent",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            const EntitySnapshotEntityState* old_entity =
                base_snapshot ? base_snapshot->find_exact(*number) : nullptr;
            const EntityBaselineState* baseline = nullptr;
            const DeltaObjectState* delta_base = nullptr;
            std::optional<EntityBaselineKey> baseline_key;
            std::optional<EntityStateBaseReference> state_base;

            if (old_entity != nullptr) {
                if (instanced || old_entity->schema_category() != category ||
                    !old_entity->object().matches_schema(*selected_schema)) {
                    return failure(PacketEntityDecodeErrorCode::schema_mismatch,
                                   "existing delta entity changed schema category or selected an explicit baseline",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                delta_base = &old_entity->object();
                baseline_key.emplace(old_entity->baseline_key());
                state_base.emplace(
                    EntityStateBaseReference::previous_snapshot_entity(*number));
            } else if (instanced) {
                if (instance_slot >= *instance_count) {
                    return failure(PacketEntityDecodeErrorCode::invalid_baseline_reference,
                                   "instanced baseline slot is outside the decoded baseline table",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                baseline_key.emplace(
                    EntityBaselineKey::for_alternate_slot(instance_slot));
                baseline = next.baselines_->find_exact(*baseline_key);
                state_base.emplace(
                    EntityStateBaseReference::instanced_baseline(instance_slot));
            } else if (intra_offset != 0U) {
                if (intra_offset > decoded_updates.size()) {
                    return failure(PacketEntityDecodeErrorCode::invalid_baseline_reference,
                                   "intra-message baseline offset precedes available records",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                const auto& prior =
                    decoded_updates[decoded_updates.size() - intra_offset];
                if (prior.schema_category() != category) {
                    return failure(PacketEntityDecodeErrorCode::schema_mismatch,
                                   "intra-message baseline has another schema category",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                if (!prior.object().matches_schema(*selected_schema)) {
                    return failure(PacketEntityDecodeErrorCode::schema_mismatch,
                                   "intra-message baseline descriptor differs from the selected schema",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                delta_base = &prior.object();
                // The owning entity identity remains its own; the exact wire
                // source is recorded separately by state_base_reference.
                baseline_key.emplace(EntityBaselineKey::for_entity(*number));
                state_base.emplace(
                    EntityStateBaseReference::intra_message_entity(
                        prior.entity_number()));
            } else {
                baseline_key.emplace(EntityBaselineKey::for_entity(*number));
                baseline = next.baselines_->find_exact(*baseline_key);
                state_base.emplace(
                    EntityStateBaseReference::entity_baseline(*number));
            }
            if (baseline != nullptr) {
                if (baseline->source_geometry().source_generation !=
                        input.source_generation ||
                    (baseline->schema_category() != category &&
                     baseline->schema_category() !=
                         EntitySchemaCategory::alternate_explicit_schema) ||
                    !baseline->object().matches_schema(*selected_schema)) {
                    return failure(PacketEntityDecodeErrorCode::schema_mismatch,
                                   "selected baseline does not match generation and entity schema",
                                   PacketEntityRecoveryStatus::none,
                                   cursor_at(record_start, bytes.size()), opcode,
                                   *number);
                }
                delta_base = &baseline->object();
            }
            if (delta_base == nullptr || !baseline_key || !state_base) {
                return failure(PacketEntityDecodeErrorCode::invalid_baseline_reference,
                               "new packet entity has no exact permitted baseline",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }

            const auto decoded = delta_decoder.decode_delta(
                *selected_schema,
                delta_base,
                DeltaValueDecodeContext{bytes, bit_cursor,
                                        bytes.size() * 8U - bit_cursor,
                                        std::nullopt, staged_server_time,
                                        false});
            if (!decoded) {
                return failure(PacketEntityDecodeErrorCode::delta_decode_failed,
                               decoded.error ? decoded.error->context
                                             : "packet entity delta decode failed",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(decoded.error
                                             ? decoded.error->bit_offset
                                             : bit_cursor,
                                         bytes.size()),
                               opcode, *number, wire_tag, resolved_base,
                               decoded.error
                                   ? std::optional{decoded.error->code}
                                   : std::nullopt);
            }
            bit_cursor = decoded.next_bit_offset;
            const bool changed = !values_equal(*decoded.state, *delta_base);
            std::shared_ptr<const DeltaObjectState> object;
            try {
                if (old_entity != nullptr && !changed) {
                    object = old_entity->object_;
                } else {
                    object = std::make_shared<const DeltaObjectState>(
                        std::move(*decoded.state));
                }
                decoded_updates.emplace_back(EntitySnapshotEntityState{
                    *number, std::move(*baseline_key), category,
                    std::move(*state_base), std::move(object)});
            } catch (const std::bad_alloc&) {
                return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                               "unable to retain decoded packet entity",
                               PacketEntityRecoveryStatus::none,
                               cursor_at(record_start, bytes.size()), opcode,
                               *number);
            }
            if (old_entity == nullptr) {
                ++added_count;
            } else if (changed) {
                ++changed_count;
            }
        }

        BitReader padding{bytes, bit_cursor,
                          bytes.size() * 8U - bit_cursor};
        if (padding.align_to_byte_zero_padding() != BitReaderError::none) {
            return failure(PacketEntityDecodeErrorCode::malformed_padding,
                           "packet-entity terminator has non-zero or truncated byte padding",
                           PacketEntityRecoveryStatus::none,
                           cursor_at(bit_cursor, bytes.size()), opcode);
        }
        bit_cursor = padding.bit_offset();
        const auto end_cursor = cursor_at(bit_cursor, bytes.size());
        if (!end_cursor) {
            return failure(PacketEntityDecodeErrorCode::size_overflow,
                           "packet-entity end cursor cannot be represented",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }

        std::vector<EntitySnapshotEntityState> entities;
        try {
            if (!delta_message) {
                entities = std::move(decoded_updates);
                added_count = entities.size();
            } else {
                entities.reserve(base_snapshot->entity_count() + added_count);
                std::size_t old_index = 0U;
                std::size_t update_index = 0U;
                std::size_t removal_index = 0U;
                while (old_index < base_snapshot->entities().size() ||
                       update_index < decoded_updates.size()) {
                    const auto old_number =
                        old_index < base_snapshot->entities().size()
                            ? base_snapshot->entities()[old_index].entity_number()
                            : (std::numeric_limits<std::uint32_t>::max)();
                    const auto update_number =
                        update_index < decoded_updates.size()
                            ? decoded_updates[update_index].entity_number()
                            : (std::numeric_limits<std::uint32_t>::max)();
                    while (removal_index < removals.size() &&
                           removals[removal_index] < old_number) {
                        ++removal_index;
                    }
                    if (update_number < old_number) {
                        entities.emplace_back(decoded_updates[update_index++]);
                        continue;
                    }
                    if (old_number ==
                        (std::numeric_limits<std::uint32_t>::max)()) {
                        break;
                    }
                    if (removal_index < removals.size() &&
                        removals[removal_index] == old_number) {
                        ++removal_index;
                        ++old_index;
                        continue;
                    }
                    if (update_number == old_number) {
                        entities.emplace_back(decoded_updates[update_index++]);
                        ++old_index;
                        continue;
                    }
                    entities.emplace_back(base_snapshot->entities()[old_index++]);
                }
            }
        } catch (const std::bad_alloc&) {
            return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                           "unable to reconstruct owning packet-entity snapshot",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        if (entities.size() > limits_.snapshots.maximum_entities_per_snapshot) {
            return failure(PacketEntityDecodeErrorCode::entity_limit_exceeded,
                           "reconstructed snapshot exceeds the configured entity limit",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        if (entities.size() != *wire_count) {
            return failure(PacketEntityDecodeErrorCode::invalid_entity_count,
                           "16-bit header count does not equal the reconstructed final entity count",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        std::size_t accounted_value_bytes = 0U;
        for (const auto& entity : entities) {
            if (entity.object().accounted_value_bytes() >
                (std::numeric_limits<std::size_t>::max)() -
                    accounted_value_bytes) {
                return failure(PacketEntityDecodeErrorCode::size_overflow,
                               "snapshot value-byte accounting overflowed",
                               PacketEntityRecoveryStatus::none, current,
                               opcode, entity.entity_number());
            }
            accounted_value_bytes += entity.object().accounted_value_bytes();
        }
        if (accounted_value_bytes >
            limits_.snapshots.maximum_snapshot_total_value_bytes) {
            return failure(PacketEntityDecodeErrorCode::total_value_bytes_exceeded,
                           "snapshot values exceed the configured retained-byte limit",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        const auto snapshot_reference =
            EntitySnapshotReference::goldsrc_transport_sequence(
                source_sequence->value());
        if (!snapshot_reference) {
            return failure(PacketEntityDecodeErrorCode::invalid_source_sequence,
                           "source sequence cannot become a snapshot reference",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        const auto unchanged_count = entities.size() >= added_count + changed_count
                                         ? entities.size() - added_count - changed_count
                                         : 0U;
        const auto geometry = EntitySourceGeometry{
            input.payload_ordinal,
            bytes.size(),
            message_start.absolute_bit_offset(),
            bit_cursor - message_start.absolute_bit_offset(),
            input.source_generation};
        std::shared_ptr<const EntitySnapshotState> snapshot;
        std::optional<EntitySnapshotHistoryBuilder> history_builder;
        try {
            snapshot = std::shared_ptr<const EntitySnapshotState>{
                new EntitySnapshotState{
                    *snapshot_reference,
                    *snapshot_time,
                    delta_message ? EntitySnapshotKind::delta
                                  : EntitySnapshotKind::full,
                    base_reference,
                    std::move(entities),
                    removals,
                    geometry,
                    EntitySnapshotStatistics{
                        static_cast<std::size_t>(*wire_count),
                        changed_count,
                        added_count,
                        removals.size(),
                        unchanged_count,
                        accounted_value_bytes},
                    kSnapshotProfile}};
            history_builder.emplace(*next.history_, limits_.snapshots);
        } catch (const std::bad_alloc&) {
            return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                           "unable to retain snapshot or transactional history",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }

        const auto inserted = history_builder->insert(*snapshot);
        if (!inserted) {
            const auto history_code = inserted.error
                                          ? inserted.error->code
                                          : EntitySnapshotHistoryErrorCode::
                                                unable_to_retain_history;
            const auto code =
                history_code == EntitySnapshotHistoryErrorCode::duplicate_snapshot
                    ? PacketEntityDecodeErrorCode::duplicate_source_frame
                    : history_code == EntitySnapshotHistoryErrorCode::old_snapshot
                          ? PacketEntityDecodeErrorCode::old_source_frame
                          : PacketEntityDecodeErrorCode::history_publish_failed;
            return failure(code,
                           inserted.error ? inserted.error->context
                                          : "snapshot history insertion failed",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        const auto published = history_builder->publish();
        if (!published) {
            return failure(PacketEntityDecodeErrorCode::history_publish_failed,
                           published.error ? published.error->context
                                           : "snapshot history publication failed",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        try {
            next.history_ =
                std::make_shared<const EntitySnapshotHistoryState>(
                    std::move(*published.state));
            next.current_snapshot_ = snapshot;
            if (next.fingerprints_.size() ==
                limits_.snapshots.maximum_snapshot_history) {
                next.fingerprints_.erase(next.fingerprints_.begin());
            }
            next.fingerprints_.push_back(
                PacketEntitySnapshotState::FrameFingerprint{
                    source_sequence->value(), hash});
            events.emplace_back(PacketEntityMessageEvent{
                delta_message ? PacketEntityMessageKind::delta
                              : PacketEntityMessageKind::full,
                *wire_count,
                wire_records,
                wire_tag,
                resolved_base,
                snapshot,
                message_start,
                *end_cursor,
                events.size()});
        } catch (const std::bad_alloc&) {
            return failure(PacketEntityDecodeErrorCode::unable_to_retain_output,
                           "unable to publish packet-entity event and history",
                           PacketEntityRecoveryStatus::none, current, opcode);
        }
        current = *end_cursor;
    }

    if (const auto control_error = control_decoder.apply_events(
            control_events, next.control_state_, !control_events.empty())) {
        return failure(PacketEntityDecodeErrorCode::runtime_control_failed,
                       control_error->context,
                       PacketEntityRecoveryStatus::none,
                       control_error->cursor,
                       control_error->wire_opcode);
    }
    const auto consumed_bytes =
        current.byte_offset() - input.initial_cursor.byte_offset();
    if (consumed_bytes >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        return failure(PacketEntityDecodeErrorCode::size_overflow,
                       "service stream consumed-bit count overflowed");
    }
    PacketEntityDecodedBatch batch{
        std::move(events),
        input.initial_cursor,
        current,
        consumed_bytes,
        consumed_bytes * 8U,
        input.source_generation,
        input.payload_ordinal,
        profile_,
        PacketEntitySpecificationSource::public_protocol_reference,
        PacketEntityStockVerification::
            not_verified_against_stock_runtime_payload};
    state = std::move(*staged_state);
    return PacketEntityDecodeResult{std::move(batch), std::nullopt};
}

std::string_view to_string(const PacketEntityCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case PacketEntityCompatibilityProfile::public_goldsrc48_packet_entities_v1:
        return "public_goldsrc48_packet_entities_v1";
    }
    return "unknown";
}

std::string_view to_string(const PacketEntitySpecificationSource source) noexcept
{
    switch (source) {
    case PacketEntitySpecificationSource::public_protocol_reference:
        return "public_protocol_reference";
    }
    return "unknown";
}

std::string_view to_string(
    const PacketEntityStockVerification verification) noexcept
{
    switch (verification) {
    case PacketEntityStockVerification::
        not_verified_against_stock_runtime_payload:
        return "not_verified_against_stock_runtime_payload";
    }
    return "unknown";
}

std::string_view to_string(const PacketEntityMessageKind kind) noexcept
{
    switch (kind) {
    case PacketEntityMessageKind::full: return "full";
    case PacketEntityMessageKind::delta: return "delta";
    }
    return "unknown";
}

std::string_view to_string(const PacketEntityRecoveryStatus status) noexcept
{
    switch (status) {
    case PacketEntityRecoveryStatus::none: return "none";
    case PacketEntityRecoveryStatus::full_snapshot_required:
        return "full_snapshot_required";
    case PacketEntityRecoveryStatus::clientdata_no_base_required:
        return "clientdata_no_base_required";
    }
    return "unknown";
}

std::string_view to_string(const PacketEntityDecodeErrorCode code) noexcept
{
    switch (code) {
    case PacketEntityDecodeErrorCode::invalid_configuration: return "invalid_configuration";
    case PacketEntityDecodeErrorCode::invalid_profile: return "invalid_profile";
    case PacketEntityDecodeErrorCode::payload_not_decompressed: return "payload_not_decompressed";
    case PacketEntityDecodeErrorCode::wrong_direction: return "wrong_direction";
    case PacketEntityDecodeErrorCode::payload_too_large: return "payload_too_large";
    case PacketEntityDecodeErrorCode::invalid_cursor: return "invalid_cursor";
    case PacketEntityDecodeErrorCode::unsupported_alignment: return "unsupported_alignment";
    case PacketEntityDecodeErrorCode::source_generation_mismatch: return "source_generation_mismatch";
    case PacketEntityDecodeErrorCode::invalid_source_sequence: return "invalid_source_sequence";
    case PacketEntityDecodeErrorCode::truncated_opcode: return "truncated_opcode";
    case PacketEntityDecodeErrorCode::unsupported_opcode: return "unsupported_opcode";
    case PacketEntityDecodeErrorCode::truncated_header: return "truncated_header";
    case PacketEntityDecodeErrorCode::invalid_entity_count: return "invalid_entity_count";
    case PacketEntityDecodeErrorCode::truncated_terminator: return "truncated_terminator";
    case PacketEntityDecodeErrorCode::truncated_entity_header: return "truncated_entity_header";
    case PacketEntityDecodeErrorCode::invalid_entity_number: return "invalid_entity_number";
    case PacketEntityDecodeErrorCode::duplicate_entity_record: return "duplicate_entity_record";
    case PacketEntityDecodeErrorCode::out_of_order_entity_record: return "out_of_order_entity_record";
    case PacketEntityDecodeErrorCode::wire_record_limit_exceeded: return "wire_record_limit_exceeded";
    case PacketEntityDecodeErrorCode::invalid_baseline_reference: return "invalid_baseline_reference";
    case PacketEntityDecodeErrorCode::schema_mismatch: return "schema_mismatch";
    case PacketEntityDecodeErrorCode::unknown_schema: return "unknown_schema";
    case PacketEntityDecodeErrorCode::missing_server_time: return "missing_server_time";
    case PacketEntityDecodeErrorCode::delta_decode_failed: return "delta_decode_failed";
    case PacketEntityDecodeErrorCode::invalid_delta_base_lookback: return "invalid_delta_base_lookback";
    case PacketEntityDecodeErrorCode::missing_delta_base: return "missing_delta_base";
    case PacketEntityDecodeErrorCode::evicted_delta_base: return "evicted_delta_base";
    case PacketEntityDecodeErrorCode::incompatible_delta_base: return "incompatible_delta_base";
    case PacketEntityDecodeErrorCode::remove_nonexistent_entity: return "remove_nonexistent_entity";
    case PacketEntityDecodeErrorCode::malformed_padding: return "malformed_padding";
    case PacketEntityDecodeErrorCode::entity_limit_exceeded: return "entity_limit_exceeded";
    case PacketEntityDecodeErrorCode::total_value_bytes_exceeded: return "total_value_bytes_exceeded";
    case PacketEntityDecodeErrorCode::duplicate_source_frame: return "duplicate_source_frame";
    case PacketEntityDecodeErrorCode::conflicting_source_frame: return "conflicting_source_frame";
    case PacketEntityDecodeErrorCode::old_source_frame: return "old_source_frame";
    case PacketEntityDecodeErrorCode::message_limit_exceeded: return "message_limit_exceeded";
    case PacketEntityDecodeErrorCode::history_publish_failed: return "history_publish_failed";
    case PacketEntityDecodeErrorCode::runtime_control_failed: return "runtime_control_failed";
    case PacketEntityDecodeErrorCode::clientdata_failed: return "clientdata_failed";
    case PacketEntityDecodeErrorCode::size_overflow: return "size_overflow";
    case PacketEntityDecodeErrorCode::unable_to_retain_output: return "unable_to_retain_output";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc

#include <hlclient/goldsrc/entity_baseline_decoder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

EntityBaselineDecodeResult fail(
    const EntityBaselineDecodeInput& input,
    const EntityBaselineDecodeErrorCode code,
    const std::size_t bit,
    std::string context,
    std::optional<std::uint32_t> entity = std::nullopt,
    std::optional<EntitySchemaCategory> category = std::nullopt)
{
    EntityBaselineDecodeResult result;
    result.start_cursor = input.start_cursor;
    result.source_metadata.payload_ordinal = input.payload_ordinal;
    result.source_metadata.direction = input.payload == nullptr
        ? NetchanDirection::server_to_client : input.payload->direction;
    result.error = EntityBaselineDecodeError{code, bit, entity, category, std::nullopt, std::move(context)};
    return result;
}

std::optional<std::string_view> schema_name_for(
    const EntityBaselineDecodeInput& input,
    const EntitySchemaCategory category) noexcept
{
    switch (category) {
    case EntitySchemaCategory::ordinary_entity: return input.ordinary_schema_name;
    case EntitySchemaCategory::player_entity: return input.player_schema_name;
    case EntitySchemaCategory::custom_entity: return input.custom_schema_name;
    case EntitySchemaCategory::alternate_explicit_schema: return input.ordinary_schema_name;
    }
    return std::nullopt;
}

} // namespace

GoldSrcEntityBaselineDecoder::GoldSrcEntityBaselineDecoder(
    const EntityBaselineDecodeLimits limits) noexcept : limits_{limits}
{
}

bool GoldSrcEntityBaselineDecoder::valid_configuration() const noexcept
{
    return valid_entity_snapshot_limits(limits_.snapshot) &&
           limits_.maximum_message_bits > 0U &&
           limits_.maximum_message_bits <= kMaximumDeltaMessageBits &&
           limits_.maximum_instanced_baselines <= 63U;
}

const EntityBaselineDecodeLimits& GoldSrcEntityBaselineDecoder::limits()
    const noexcept
{
    return limits_;
}

EntityBaselineDecodeResult GoldSrcEntityBaselineDecoder::decode(
    const EntityBaselineDecodeInput& input,
    const DeltaSchemaRegistryState& schemas) const
{
    if (!valid_configuration()) {
        return fail(input, EntityBaselineDecodeErrorCode::invalid_configuration, 0U,
                    "Invalid entity baseline decoder limits");
    }
    if (input.payload == nullptr ||
        !service_payload_decode_ready(*input.payload)) {
        return fail(input, EntityBaselineDecodeErrorCode::payload_not_decompressed, 0U,
                    "svc_spawnbaseline requires an owning decompressed service payload");
    }
    if (input.payload->direction != NetchanDirection::server_to_client) {
        return fail(input, EntityBaselineDecodeErrorCode::wrong_direction, 0U,
                    "svc_spawnbaseline is a server-to-client message");
    }
    if (input.source_generation == 0U) {
        return fail(input, EntityBaselineDecodeErrorCode::invalid_generation, 0U,
                    "Server generation must be non-zero");
    }
    const auto& bytes = input.payload->bytes;
    const auto start = input.start_cursor.absolute_bit_offset();
    if (!valid_stock_runtime_source_cursor(input.start_cursor, bytes.size()) ||
        start > bytes.size() * 8U) {
        return fail(input, EntityBaselineDecodeErrorCode::invalid_cursor, start,
                    "Baseline start cursor is outside the owning payload");
    }
    const auto available = bytes.size() * 8U - start;
    const auto message_bits = input.message_bit_length == static_cast<std::size_t>(-1)
        ? available : input.message_bit_length;
    if (message_bits > available || message_bits > limits_.maximum_message_bits) {
        return fail(input, EntityBaselineDecodeErrorCode::message_too_large, start,
                    "Baseline message exceeds bounded payload geometry");
    }
    if (input.max_clients == 0U || input.max_clients > 255U) {
        return fail(input, EntityBaselineDecodeErrorCode::invalid_configuration, start,
                    "max_clients is outside the supported GoldSrc context");
    }
    EntityBaselineRegistryBuilder builder{
        schemas, limits_.snapshot,
        EntitySnapshotCompatibilityProfile::public_goldsrc48_entity_delta_v1};
    if (!builder.valid_configuration()) {
        return fail(input, EntityBaselineDecodeErrorCode::invalid_configuration, start,
                    "Baseline registry configuration is invalid");
    }
    std::size_t cursor = start;
    std::size_t entities = 0U;
    std::size_t instanced = 0U;
    GoldSrcDeltaValueDecoder delta{
        {}, DeltaValueCompatibilityProfile::public_goldsrc48_entity_delta_v1};

    auto decode_record = [&](const EntityBaselineKey key,
                             const EntitySchemaCategory category,
                             const std::uint32_t entity_number) -> std::optional<EntityBaselineDecodeResult> {
        const auto schema_name = schema_name_for(input, category);
        const auto* schema = schema_name.has_value() ? schemas.find_exact(*schema_name) : nullptr;
        if (schema == nullptr) {
            return fail(input, EntityBaselineDecodeErrorCode::unknown_schema, cursor,
                        "GoldSrc baseline schema is absent from the validated registry",
                        entity_number, category);
        }
        auto null_state = DeltaObjectBuilder{
            {}, DeltaValueCompatibilityProfile::public_goldsrc48_entity_delta_v1}
                              .build_default(*schema);
        if (!null_state) {
            return fail(input, EntityBaselineDecodeErrorCode::registry_publish_failed,
                        cursor, "Unable to construct the protocol-defined null baseline",
                        entity_number, category);
        }
        const auto record_start = cursor;
        DeltaValueDecodeContext context{
            bytes, cursor, start + message_bits - cursor, {},
            kGoldSrcInitialBaselineTimeBaseSeconds, false};
        const auto decoded = delta.decode_delta(*schema, &*null_state.state, context);
        if (!decoded) {
            auto result = fail(input, EntityBaselineDecodeErrorCode::delta_decode_failed,
                               decoded.error.has_value() ? decoded.error->bit_offset : cursor,
                               "GoldSrc baseline delta value decode failed",
                               entity_number, category);
            if (decoded.error.has_value()) {
                result.error->delta_error = decoded.error->code;
                result.error->context += ": ";
                result.error->context += decoded.error->context;
            }
            return result;
        }
        cursor = decoded.next_bit_offset;
        const auto geometry = EntitySourceGeometry{
            input.payload_ordinal, bytes.size(), record_start,
            decoded.bits_consumed, input.source_generation};
        const auto inserted = builder.insert(key, category, *decoded.state, geometry);
        if (!inserted) {
            const auto error_code =
                (inserted.error.has_value() &&
                 inserted.error->code ==
                     hlclient::goldsrc::EntityBaselineErrorCode::duplicate_baseline_identity)
                    ? EntityBaselineDecodeErrorCode::duplicate_baseline
                    : EntityBaselineDecodeErrorCode::registry_publish_failed;
            return fail(input, error_code, record_start,
                        "Baseline registry rejected decoded record", entity_number,
                        category);
        }
        return std::nullopt;
    };

    while (true) {
        BitReader terminator_reader{bytes, cursor, start + message_bits - cursor};
        const auto terminator =
            terminator_reader.read_bits(kGoldSrcBaselineTerminatorBits);
        if (!terminator) {
            return fail(input, EntityBaselineDecodeErrorCode::truncated_terminator,
                        cursor,
                        "svc_spawnbaseline requires a complete 16-bit terminator lookahead");
        }
        if (terminator.value == kGoldSrcBaselineTerminator) {
            cursor = terminator_reader.bit_offset();
            break;
        }

        BitReader reader{bytes, cursor, start + message_bits - cursor};
        const auto number = reader.read_bits(kGoldSrcBaselineEntityNumberBits);
        if (!number) {
            return fail(input, EntityBaselineDecodeErrorCode::truncated_entity_number,
                        cursor, "svc_spawnbaseline entity number is truncated");
        }
        cursor = reader.bit_offset();
        const auto type = reader.read_bits(2U);
        if (!type) {
            return fail(input, EntityBaselineDecodeErrorCode::truncated_entity_type,
                        cursor, "svc_spawnbaseline entity type is truncated", number.value);
        }
        cursor = reader.bit_offset();
        const auto category = (type.value & 2U) != 0U
            ? EntitySchemaCategory::custom_entity
            : ((number.value > 0U && number.value <= input.max_clients)
                   ? EntitySchemaCategory::player_entity
                   : EntitySchemaCategory::ordinary_entity);
        if (++entities > limits_.snapshot.maximum_entities_per_snapshot) {
            return fail(input, EntityBaselineDecodeErrorCode::message_too_large, cursor,
                        "svc_spawnbaseline entity count exceeds the configured limit",
                        number.value, category);
        }
        if (const auto error = decode_record(EntityBaselineKey::for_entity(number.value),
                                             category, number.value)) {
            return *error;
        }
    }

    BitReader count_reader{bytes, cursor, start + message_bits - cursor};
    const auto count = count_reader.read_bits(6U);
    if (!count) {
        return fail(input, EntityBaselineDecodeErrorCode::truncated_instanced_count,
                    cursor, "svc_spawnbaseline instanced baseline count is truncated");
    }
    cursor = count_reader.bit_offset();
    if (count.value > limits_.maximum_instanced_baselines) {
        return fail(input, EntityBaselineDecodeErrorCode::instanced_count_limit_exceeded,
                    cursor, "Instanced baseline count exceeds the configured limit");
    }
    for (std::uint32_t slot = 0U; slot < count.value; ++slot) {
        if (++instanced + entities > limits_.snapshot.maximum_entities_per_snapshot) {
            return fail(input, EntityBaselineDecodeErrorCode::message_too_large, cursor,
                        "Total baseline count exceeds the configured limit");
        }
        if (const auto error = decode_record(EntityBaselineKey::for_alternate_slot(slot),
                                             EntitySchemaCategory::alternate_explicit_schema,
                                             slot)) {
            return *error;
        }
    }

    BitReader padding{bytes, cursor, start + message_bits - cursor};
    if (padding.align_to_byte_zero_padding() != BitReaderError::none) {
        return fail(input, EntityBaselineDecodeErrorCode::malformed_padding, cursor,
                    "svc_spawnbaseline has non-zero or truncated byte padding");
    }
    cursor = padding.bit_offset();
    const auto end_cursor = StockRuntimeSourceCursor::create(cursor / 8U, cursor % 8U, bytes.size());
    if (!end_cursor.has_value()) {
        return fail(input, EntityBaselineDecodeErrorCode::size_overflow, cursor,
                    "Baseline end cursor cannot be represented");
    }
    auto published = std::move(builder).publish();
    if (!published) {
        return fail(input, EntityBaselineDecodeErrorCode::registry_publish_failed, cursor,
                    "Baseline registry publication failed");
    }
    EntityBaselineDecodeResult result;
    result.registry.emplace(std::move(*published.state));
    result.start_cursor = input.start_cursor;
    result.end_cursor = *end_cursor;
    result.entity_count = entities;
    result.instanced_count = instanced;
    result.bits_consumed = cursor - start;
    result.source_metadata = StockRuntimeSourceMetadata{
        input.payload_ordinal, input.payload->direction,
        input.payload->source_sequence, input.payload->source_acknowledgement,
        input.payload->source_reliable, input.payload->acknowledgement_reliable,
        input.payload->reassembled, input.payload->decompressed, bytes.size()};
    return result;
}

} // namespace hlclient::goldsrc

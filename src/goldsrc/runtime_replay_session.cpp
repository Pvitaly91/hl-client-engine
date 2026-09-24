#include <hlclient/goldsrc/runtime_replay_session.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include <variant>

namespace hlclient::goldsrc {
namespace {

using client::RuntimeClientObservationState;
using client::RuntimeObservationCompleteness;
using client::RuntimeObservationFreshness;
using client::RuntimeObservationSource;
using client::RuntimePacketEntityObservation;
using client::RuntimeReceivingClientObservation;
using client::RuntimeSubstateMetadata;
using client::RuntimeVector3Observation;
using client::RuntimeWeaponSlotObservation;

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

[[nodiscard]] RuntimeReplayError failure(
    const RuntimeReplayErrorCode code,
    std::string context,
    const RuntimeReplayRecoveryStatus recovery =
        RuntimeReplayRecoveryStatus::none) noexcept
{
    RuntimeReplayError result;
    result.code = code;
    result.recovery = recovery;
    result.context = std::move(context);
    return result;
}

[[nodiscard]] bool valid_profile(
    const RuntimeReplayCompatibilityProfile profile) noexcept
{
    return profile == RuntimeReplayCompatibilityProfile::
                          public_goldsrc48_runtime_replay_v1;
}

[[nodiscard]] bool valid_bindings(
    const RuntimeReplaySchemaBindings& bindings) noexcept
{
    return !bindings.ordinary_entity.empty() &&
        !bindings.player_entity.empty() && !bindings.custom_entity.empty() &&
        !bindings.client_data.empty() && !bindings.weapon_data.empty();
}

[[nodiscard]] bool valid_initialization(
    const RuntimeReplayInitialization& initialization) noexcept
{
    if (!valid_profile(initialization.profile) || initialization.generation == 0U ||
        initialization.max_clients == 0U ||
        initialization.max_clients > 255U || !initialization.schemas ||
        !initialization.baselines || !valid_bindings(initialization.schema_bindings) ||
        !valid_runtime_replay_limits(initialization.limits) ||
        !valid_runtime_user_message_definitions(
            initialization.user_message_definitions)) {
        return false;
    }
    return true;
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= kFnvPrime;
}

template <typename Value>
void hash_unsigned(std::uint64_t& hash, const Value value) noexcept
{
    auto remaining = static_cast<std::uint64_t>(value);
    for (std::size_t index = 0U; index < sizeof(Value); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(remaining & 0xffU));
        remaining >>= 8U;
    }
}

[[nodiscard]] std::uint64_t record_hash(
    const RuntimeReplayRecord& record) noexcept
{
    std::uint64_t hash = kFnvOffset;
    hash_unsigned(hash, record.generation);
    hash_unsigned(hash, record.record_ordinal);
    hash_unsigned(hash, record.payload.source_sequence);
    hash_unsigned(hash, record.payload.source_acknowledgement);
    hash_unsigned(hash, record.initial_cursor.absolute_bit_offset());
    hash_byte(hash, record.payload.source_reliable ? 1U : 0U);
    hash_byte(hash, record.payload.reassembled ? 1U : 0U);
    hash_byte(hash, record.payload.decompressed ? 1U : 0U);
    hash_byte(hash, record.payload.acknowledgement_reliable ? 1U : 0U);
    hash_byte(hash, static_cast<std::uint8_t>(record.payload.direction));
    for (const auto value : record.payload.bytes) {
        hash_byte(hash, std::to_integer<std::uint8_t>(value));
    }
    return hash;
}

[[nodiscard]] RuntimeObservationSource source_for(
    const RuntimeReplayRecord& record,
    const StockRuntimeSourceCursor& start,
    const StockRuntimeSourceCursor& end) noexcept
{
    return RuntimeObservationSource{
        record.record_identity,
        record.record_ordinal,
        record.payload.source_sequence,
        start.absolute_bit_offset(),
        end.absolute_bit_offset(),
        record.payload.source_acknowledgement,
        record.payload.source_reliable,
        record.payload.reassembled};
}

[[nodiscard]] RuntimeSubstateMetadata unavailable_metadata(
    const std::uint64_t generation) noexcept
{
    return RuntimeSubstateMetadata{
        generation,
        RuntimeObservationFreshness::unavailable,
        RuntimeObservationCompleteness::unavailable,
        std::nullopt};
}

[[nodiscard]] RuntimeSubstateMetadata observed_metadata(
    const std::uint64_t generation,
    RuntimeObservationSource source) noexcept
{
    return RuntimeSubstateMetadata{
        generation,
        RuntimeObservationFreshness::observed_in_record,
        RuntimeObservationCompleteness::complete_reconstruction,
        std::move(source)};
}

[[nodiscard]] RuntimeSubstateMetadata retained_metadata(
    RuntimeSubstateMetadata metadata) noexcept
{
    if (metadata.freshness != RuntimeObservationFreshness::unavailable) {
        metadata.freshness = RuntimeObservationFreshness::retained;
    }
    return metadata;
}

struct ExpectedField final {
    DeltaFieldBaseType base_type{DeltaFieldBaseType::float_value};
    bool signed_value{false};
    std::optional<DeltaFieldBaseType> alternate_base_type;
    std::optional<bool> alternate_signed_value;
};

[[nodiscard]] const DeltaFieldDefinition* find_definition(
    const DeltaSchema& schema,
    const std::string_view name) noexcept
{
    const auto found = std::find_if(
        schema.fields().begin(), schema.fields().end(),
        [name](const DeltaFieldDefinition& field) {
            return field.name() == name;
        });
    return found == schema.fields().end() ? nullptr : &*found;
}

[[nodiscard]] bool matches_expected(
    const DeltaFieldDefinition& field,
    const ExpectedField& expected) noexcept
{
    const auto& flags = field.type_flags();
    if (flags.base_type() == expected.base_type &&
        flags.signed_value() == expected.signed_value) {
        return true;
    }
    return expected.alternate_base_type && expected.alternate_signed_value &&
        flags.base_type() == *expected.alternate_base_type &&
        flags.signed_value() == *expected.alternate_signed_value;
}

struct OptionalDoubleResult final {
    std::optional<double> value;
    std::optional<RuntimeReplayError> error;
};

[[nodiscard]] OptionalDoubleResult optional_double(
    const DeltaSchema& schema,
    const DeltaObjectState& object,
    const std::string_view name,
    const ExpectedField expected) noexcept
{
    const auto* definition = find_definition(schema, name);
    if (definition == nullptr) {
        return {};
    }
    if (!matches_expected(*definition, expected)) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_schema_mismatch,
            "semantic field descriptor does not match the public reference binding: " +
                std::string{name})};
    }
    const auto* field = object.find_exact(name);
    if (field == nullptr || !std::holds_alternative<double>(field->value())) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_value_mismatch,
            "semantic field value is absent or has the wrong canonical type: " +
                std::string{name})};
    }
    const auto value = std::get<double>(field->value());
    if (!std::isfinite(value)) {
        return {{}, failure(RuntimeReplayErrorCode::non_finite_semantic_value,
            "semantic field value is not finite: " + std::string{name})};
    }
    return {value, std::nullopt};
}

struct OptionalSignedResult final {
    std::optional<std::int32_t> value;
    std::optional<RuntimeReplayError> error;
};

struct OptionalUnsignedResult final {
    std::optional<std::uint32_t> value;
    std::optional<RuntimeReplayError> error;
};

[[nodiscard]] OptionalUnsignedResult optional_unsigned(
    const DeltaSchema& schema,
    const DeltaObjectState& object,
    const std::string_view name) noexcept
{
    const auto* definition = find_definition(schema, name);
    if (definition == nullptr) return {};
    if (!matches_expected(*definition,
            {DeltaFieldBaseType::integer_value, false, {}, {}})) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_schema_mismatch,
            "semantic unsigned-integer descriptor mismatch: " + std::string{name})};
    }
    const auto* field = object.find_exact(name);
    if (field == nullptr ||
        !std::holds_alternative<std::uint32_t>(field->value())) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_value_mismatch,
            "semantic unsigned-integer value mismatch: " + std::string{name})};
    }
    return {std::get<std::uint32_t>(field->value()), std::nullopt};
}

[[nodiscard]] OptionalSignedResult optional_signed(
    const DeltaSchema& schema,
    const DeltaObjectState& object,
    const std::string_view name) noexcept
{
    const auto* definition = find_definition(schema, name);
    if (definition == nullptr) {
        return {};
    }
    if (!matches_expected(*definition,
            {DeltaFieldBaseType::integer_value, true, {}, {}})) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_schema_mismatch,
            "semantic signed-integer descriptor mismatch: " +
                std::string{name})};
    }
    const auto* field = object.find_exact(name);
    if (field == nullptr ||
        !std::holds_alternative<std::int32_t>(field->value())) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_value_mismatch,
            "semantic signed-integer value mismatch: " + std::string{name})};
    }
    return {std::get<std::int32_t>(field->value()), std::nullopt};
}

struct OptionalBoolResult final {
    std::optional<bool> value;
    std::optional<RuntimeReplayError> error;
};

[[nodiscard]] OptionalBoolResult optional_bool(
    const DeltaSchema& schema,
    const DeltaObjectState& object,
    const std::string_view name) noexcept
{
    const auto* definition = find_definition(schema, name);
    if (definition == nullptr) {
        return {};
    }
    if (!matches_expected(*definition,
            {DeltaFieldBaseType::integer_value, false, {}, {}})) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_schema_mismatch,
            "semantic boolean descriptor mismatch: " + std::string{name})};
    }
    const auto* field = object.find_exact(name);
    if (field == nullptr ||
        !std::holds_alternative<std::uint32_t>(field->value())) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_value_mismatch,
            "semantic boolean value mismatch: " + std::string{name})};
    }
    const auto value = std::get<std::uint32_t>(field->value());
    if (value > 1U) {
        return {{}, failure(RuntimeReplayErrorCode::semantic_value_mismatch,
            "semantic boolean value is outside 0..1: " + std::string{name})};
    }
    return {value != 0U, std::nullopt};
}

[[nodiscard]] const DeltaSchema* schema_for(
    const DeltaSchemaRegistryState& registry,
    const DeltaObjectState& object) noexcept
{
    const auto* schema = registry.find_exact(object.schema_name());
    return schema != nullptr && object.matches_schema(*schema) ? schema : nullptr;
}

[[nodiscard]] std::optional<RuntimeReplayError> project_vector(
    const DeltaSchema& schema,
    const DeltaObjectState& object,
    const std::string_view x,
    const std::string_view y,
    const std::string_view z,
    const ExpectedField expected,
    RuntimeVector3Observation& target) noexcept
{
    const auto px = optional_double(schema, object, x, expected);
    if (px.error) return px.error;
    const auto py = optional_double(schema, object, y, expected);
    if (py.error) return py.error;
    const auto pz = optional_double(schema, object, z, expected);
    if (pz.error) return pz.error;
    target = {px.value, py.value, pz.value};
    return std::nullopt;
}

struct ProjectionResult final {
    std::shared_ptr<const RuntimeClientObservationState> state;
    std::optional<RuntimeReplayError> error;
};

[[nodiscard]] std::optional<RuntimeReplayError> project_visual_fields(
    const DeltaSchema& schema, const DeltaObjectState& object,
    RuntimePacketEntityObservation& target)
{
    // Custom beam schemas reuse names with different meanings; they must not
    // be projected as ordinary Studio/Sprite fields.
    if (schema.name() != "entity_state_t" && schema.name() != "entity_state_player_t") { return {}; }
    target.ordinary_visual_schema = true;
    const auto unsigned_field = [&](std::string_view name, DeltaFieldBaseType type,
        std::optional<std::uint32_t>& output) -> std::optional<RuntimeReplayError> {
        const auto* definition = find_definition(schema, name);
        if (!definition) { return {}; }
        const auto* field = object.find_exact(name);
        if (!matches_expected(*definition, {type, false, {}, {}}) ||
            !field || !std::holds_alternative<std::uint32_t>(field->value())) {
            return failure(RuntimeReplayErrorCode::semantic_schema_mismatch,
                "public visual field schema/type mismatch: " + std::string{name});
        }
        output = std::get<std::uint32_t>(field->value());
        return {};
    };
    for (const auto& [name, output] : std::array{
        std::pair{"modelindex", &target.model_index}, std::pair{"sequence", &target.sequence},
        std::pair{"body", &target.body}, std::pair{"rendermode", &target.render_mode},
        std::pair{"effects", &target.effects}}) {
        if (auto error = unsigned_field(name, DeltaFieldBaseType::integer_value, *output)) { return error; }
    }
    const auto frame = optional_double(schema, object, "frame", {DeltaFieldBaseType::float_value, false, {}, {}});
    if (frame.error) { return frame.error; }
    target.frame = frame.value;
    if (const auto* definition = find_definition(schema, "skin")) {
        const auto* field = object.find_exact("skin");
        if (!matches_expected(*definition, {DeltaFieldBaseType::short_value, true, {}, {}}) ||
            !field || !std::holds_alternative<std::int32_t>(field->value())) {
            return failure(RuntimeReplayErrorCode::semantic_schema_mismatch, "public visual skin schema/type mismatch");
        }
        target.skin = std::get<std::int32_t>(field->value());
    }
    constexpr std::array controllers{"controller[0]", "controller[1]", "controller[2]", "controller[3]"};
    constexpr std::array blending{"blending[0]", "blending[1]"};
    for (std::size_t i=0; i<controllers.size(); ++i) {
        if (auto error = unsigned_field(controllers[i], DeltaFieldBaseType::byte_value, target.controllers[i])) { return error; }
    }
    for (std::size_t i=0; i<blending.size(); ++i) {
        if (auto error = unsigned_field(blending[i], DeltaFieldBaseType::byte_value, target.blending[i])) { return error; }
    }
    return {};
}

[[nodiscard]] std::optional<RuntimeReplayError> project_player_movement_fields(
    const DeltaSchema& schema, const DeltaObjectState& object,
    RuntimePacketEntityObservation& target)
{
    if (schema.name() != "entity_state_player_t") return {};
    target.player_movement_schema = true;
    const auto move_type = optional_unsigned(schema, object, "movetype");
    if (move_type.error) return move_type.error;
    target.move_type = move_type.value;
    const auto use_hull = optional_unsigned(schema, object, "usehull");
    if (use_hull.error) return use_hull.error;
    target.use_hull = use_hull.value;
    const auto gravity = optional_double(schema, object, "gravity",
        {DeltaFieldBaseType::float_value, true, {}, {}});
    if (gravity.error) return gravity.error;
    target.gravity_multiplier = gravity.value;
    const auto friction = optional_double(schema, object, "friction",
        {DeltaFieldBaseType::float_value, true, {}, {}});
    if (friction.error) return friction.error;
    target.friction_multiplier = friction.value;
    if (const auto error = project_vector(schema, object, "basevelocity[0]",
            "basevelocity[1]", "basevelocity[2]",
            {DeltaFieldBaseType::float_value, true, {}, {}},
            target.base_velocity)) return error;
    const auto spectator = optional_bool(schema, object, "spectator");
    if (spectator.error) return spectator.error;
    target.spectator = spectator.value;
    return {};
}

[[nodiscard]] ProjectionResult project_candidate(
    const PacketEntitySnapshotState& decoder_state,
    const DeltaSchemaRegistryState& schemas,
    const RuntimeReplayRecord* record,
    const PacketEntityDecodedBatch* batch,
    const RuntimeClientObservationState* previous,
    const std::uint64_t publication_revision,
    const RuntimeReplayLimits& limits)
{
    try {
        auto candidate = std::make_shared<RuntimeClientObservationState>();
        candidate->generation = decoder_state.source_generation();
        candidate->publication_revision = publication_revision;
        candidate->server_time_metadata = unavailable_metadata(candidate->generation);
        candidate->entity_metadata = unavailable_metadata(candidate->generation);
        candidate->client_metadata = unavailable_metadata(candidate->generation);

        const RuntimeControlEvent* time_event = nullptr;
        const RuntimeControlEvent* view_angle_event = nullptr;
        const ClientDataMessageEvent* client_event = nullptr;
        const PacketEntityMessageEvent* entity_event = nullptr;
        if (batch != nullptr) {
            for (const auto& event : batch->events) {
                if (const auto* control = std::get_if<RuntimeControlEvent>(&event);
                    control != nullptr) {
                    if (control->kind == RuntimeControlMessageKind::server_time) {
                        time_event = control;
                    } else if (control->kind ==
                               RuntimeControlMessageKind::view_angles) {
                        view_angle_event = control;
                    }
                } else if (const auto* client =
                               std::get_if<ClientDataMessageEvent>(&event)) {
                    client_event = client;
                } else if (const auto* entity =
                               std::get_if<PacketEntityMessageEvent>(&event)) {
                    entity_event = entity;
                }
            }
        }

        if (const auto& server_time = decoder_state.control_state().server_time()) {
            candidate->server_time_seconds = server_time->seconds;
            if (time_event != nullptr && record != nullptr) {
                candidate->server_time_metadata = observed_metadata(
                    candidate->generation,
                    source_for(*record, time_event->provenance.start_cursor,
                               time_event->provenance.end_cursor));
            } else if (previous != nullptr) {
                candidate->server_time_metadata =
                    retained_metadata(previous->server_time_metadata);
            }
        }

        if (view_angle_event != nullptr && record != nullptr) {
            const auto* angles =
                std::get_if<RuntimeControlViewAngles>(&view_angle_event->body);
            if (angles == nullptr) {
                return {{}, failure(
                    RuntimeReplayErrorCode::semantic_schema_mismatch,
                    "typed svc_setangle event lost its angle body")};
            }
            constexpr double kAngleScale = 360.0 / 65536.0;
            candidate->view_angle_correction =
                client::RuntimeViewAngleCorrection{
                    static_cast<double>(angles->angle_shorts[0U]) * kAngleScale,
                    static_cast<double>(angles->angle_shorts[1U]) * kAngleScale,
                    static_cast<double>(angles->angle_shorts[2U]) * kAngleScale,
                    source_for(*record, view_angle_event->provenance.start_cursor,
                               view_angle_event->provenance.end_cursor)};
        }

        if (const auto& snapshot = decoder_state.current_snapshot()) {
            if (snapshot->entity_count() > limits.maximum_projected_entities) {
                return {{}, failure(
                    RuntimeReplayErrorCode::projection_limit_exceeded,
                    "packet-entity observation exceeds the bridge limit")};
            }
            candidate->packet_entities.reserve(snapshot->entity_count());
            for (const auto& entity : snapshot->entities()) {
                const auto* schema = schema_for(schemas, entity.object());
                if (schema == nullptr) {
                    return {{}, failure(
                        RuntimeReplayErrorCode::semantic_schema_mismatch,
                        "entity object does not match its exact registered schema")};
                }
                RuntimePacketEntityObservation projected;
                projected.entity_number = entity.entity_number();
                if (const auto error = project_vector(
                        *schema, entity.object(), "origin[0]", "origin[1]",
                        "origin[2]",
                        {DeltaFieldBaseType::float_value, true, {}, {}},
                        projected.origin)) {
                    return {{}, error};
                }
                if (const auto error = project_vector(
                        *schema, entity.object(), "angles[0]", "angles[1]",
                        "angles[2]",
                        {DeltaFieldBaseType::angle, false,
                         DeltaFieldBaseType::float_value, true},
                        projected.angles)) {
                    return {{}, error};
                }
                if (const auto error = project_visual_fields(*schema, entity.object(), projected)) { return {{}, error}; }
                if (const auto error = project_player_movement_fields(*schema, entity.object(), projected)) { return {{}, error}; }
                candidate->packet_entities.push_back(std::move(projected));
            }
            if (entity_event != nullptr && record != nullptr) {
                candidate->entity_metadata = observed_metadata(
                    candidate->generation,
                    source_for(*record, entity_event->start_cursor,
                               entity_event->end_cursor));
            } else if (previous != nullptr) {
                candidate->entity_metadata =
                    retained_metadata(previous->entity_metadata);
            }
        }

        const auto& client_frame =
            decoder_state.client_data_state().current_frame();
        if (client_frame) {
            const auto* client_schema =
                schema_for(schemas, client_frame->client_data());
            if (client_schema == nullptr) {
                return {{}, failure(
                    RuntimeReplayErrorCode::semantic_schema_mismatch,
                    "clientdata object does not match its exact registered schema")};
            }
            RuntimeReceivingClientObservation receiving;
            const auto health = optional_double(
                *client_schema, client_frame->client_data(), "health",
                {DeltaFieldBaseType::float_value, true, {}, {}});
            if (health.error) return {{}, health.error};
            receiving.health = health.value;
            if (const auto error = project_vector(
                    *client_schema, client_frame->client_data(),
                    "origin[0]", "origin[1]", "origin[2]",
                    {DeltaFieldBaseType::float_value, true, {}, {}},
                    receiving.origin)) {
                return {{}, error};
            }
            if (const auto error = project_vector(
                    *client_schema, client_frame->client_data(),
                    "velocity[0]", "velocity[1]", "velocity[2]",
                    {DeltaFieldBaseType::float_value, true, {}, {}},
                    receiving.velocity)) {
                return {{}, error};
            }
            if (const auto error = project_vector(
                    *client_schema, client_frame->client_data(),
                    "view_ofs[0]", "view_ofs[1]", "view_ofs[2]",
                    {DeltaFieldBaseType::float_value, true, {}, {}},
                    receiving.view_offset)) {
                return {{}, error};
            }
            for (const auto& [name, output] : std::array{
                    std::pair{"flags", &receiving.flags},
                    std::pair{"flDuckTime", &receiving.duck_time},
                    std::pair{"waterlevel", &receiving.water_level},
                    std::pair{"deadflag", &receiving.dead_flag}}) {
                const auto value = optional_unsigned(*client_schema,
                    client_frame->client_data(), name);
                if (value.error) return {{}, value.error};
                *output = value.value;
            }
            const auto maxspeed = optional_double(*client_schema,
                client_frame->client_data(), "maxspeed",
                {DeltaFieldBaseType::float_value, false, {}, {}});
            if (maxspeed.error) return {{}, maxspeed.error};
            receiving.maximum_speed = maxspeed.value;
            const auto in_duck = optional_bool(*client_schema,
                client_frame->client_data(), "bInDuck");
            if (in_duck.error) return {{}, in_duck.error};
            receiving.in_duck = in_duck.value;
            candidate->receiving_client = std::move(receiving);
            candidate->weapon_slots.reserve(client_frame->weapon_slots().size());
            for (const auto& slot : client_frame->weapon_slots()) {
                const auto* weapon_schema = schema_for(schemas, slot.object());
                if (weapon_schema == nullptr) {
                    return {{}, failure(
                        RuntimeReplayErrorCode::semantic_schema_mismatch,
                        "weapon object does not match its exact registered schema")};
                }
                RuntimeWeaponSlotObservation weapon;
                weapon.wire_slot = slot.wire_index();
                const auto clip = optional_signed(
                    *weapon_schema, slot.object(), "m_iClip");
                if (clip.error) return {{}, clip.error};
                weapon.clip = clip.value;
                const auto reload = optional_bool(
                    *weapon_schema, slot.object(), "m_fInReload");
                if (reload.error) return {{}, reload.error};
                weapon.in_reload = reload.value;
                const auto next_reload = optional_double(
                    *weapon_schema, slot.object(), "m_flNextReload",
                    {DeltaFieldBaseType::float_value, true, {}, {}});
                if (next_reload.error) return {{}, next_reload.error};
                weapon.next_reload = next_reload.value;
                const auto next_primary = optional_double(
                    *weapon_schema, slot.object(), "m_flNextPrimaryAttack",
                    {DeltaFieldBaseType::float_value, true, {}, {}});
                if (next_primary.error) return {{}, next_primary.error};
                weapon.next_primary_attack = next_primary.value;
                candidate->weapon_slots.push_back(std::move(weapon));
            }
            if (client_event != nullptr && record != nullptr) {
                candidate->client_metadata = observed_metadata(
                    candidate->generation,
                    source_for(*record, client_event->start_cursor,
                               client_event->end_cursor));
            } else if (previous != nullptr) {
                candidate->client_metadata =
                    retained_metadata(previous->client_metadata);
            }
        }

        candidate->canonical_state_hash =
            client::runtime_observation_canonical_hash(*candidate);
        if (!client::valid_runtime_observation(*candidate)) {
            return {{}, failure(
                RuntimeReplayErrorCode::bridge_rejected_candidate,
                "projected runtime observation violates the client boundary")};
        }
        return {std::move(candidate), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {{}, failure(RuntimeReplayErrorCode::unable_to_retain_candidate,
            "unable to retain the staged runtime projection")};
    }
}

[[nodiscard]] RuntimeReplayRecoveryStatus recovery_for(
    const PacketEntityDecodeError& error) noexcept
{
    switch (error.recovery) {
    case PacketEntityRecoveryStatus::none:
        return RuntimeReplayRecoveryStatus::none;
    case PacketEntityRecoveryStatus::full_snapshot_required:
        return RuntimeReplayRecoveryStatus::entity_full_snapshot_required;
    case PacketEntityRecoveryStatus::clientdata_no_base_required:
        return RuntimeReplayRecoveryStatus::clientdata_no_base_required;
    }
    return RuntimeReplayRecoveryStatus::none;
}

} // namespace

bool valid_runtime_replay_limits(const RuntimeReplayLimits& limits) noexcept
{
    return valid_packet_entity_decode_limits(limits.decoder) &&
        limits.maximum_projected_entities != 0U &&
        limits.maximum_projected_entities <=
            kMaximumRuntimeReplayProjectedEntities &&
        limits.maximum_record_fingerprints != 0U &&
        limits.maximum_record_fingerprints <=
            kMaximumRuntimeReplayRecordFingerprints;
}

RuntimeReplaySession::RuntimeReplaySession(
    RuntimeReplayInitialization initialization,
    PacketEntitySnapshotState decoder_state,
    GoldSrcPacketEntityDecoder decoder,
    client::ClientWorldState& target,
    const std::uint64_t publication_revision,
    const std::uint64_t committed_state_hash) noexcept
    : initialization_{std::move(initialization)},
      decoder_state_{std::move(decoder_state)},
      decoder_{std::move(decoder)},
      target_{&target},
      publication_revision_{publication_revision},
      committed_state_hash_{committed_state_hash}
{
}

RuntimeReplayInitializeResult RuntimeReplaySession::initialize(
    RuntimeReplayInitialization initialization,
    client::ClientWorldState& target)
{
    if (!valid_initialization(initialization)) {
        return {{}, failure(RuntimeReplayErrorCode::invalid_configuration,
            "runtime replay initialization is invalid")};
    }
    if (target.runtime_observation()) {
        return {{}, failure(
            RuntimeReplayErrorCode::target_already_has_runtime_observation,
            "ClientWorldState already owns a runtime observation")};
    }
    try {
        PacketEntitySnapshotState decoder_state{
            initialization.generation, initialization.max_clients,
            initialization.schemas, initialization.baselines,
            initialization.limits.decoder.snapshots,
            initialization.limits.decoder.client_data};
        GoldSrcPacketEntityDecoder decoder{initialization.limits.decoder};
        if (!decoder_state.valid() || !decoder.valid_configuration()) {
            return {{}, failure(RuntimeReplayErrorCode::invalid_configuration,
                "A/B/C/D decoder state cannot be initialized")};
        }
        const auto projected = project_candidate(
            decoder_state, *initialization.schemas, nullptr, nullptr, nullptr,
            1U, initialization.limits);
        if (projected.error || !projected.state) {
            return {{}, projected.error ? std::move(projected.error)
                                        : std::optional<RuntimeReplayError>{
                                              failure(RuntimeReplayErrorCode::
                                                  bridge_rejected_candidate,
                                                  "initial projection failed")}};
        }
        client::ClientWorldState world_candidate = target;
        if (!world_candidate.publish_runtime_observation(projected.state)) {
            return {{}, failure(RuntimeReplayErrorCode::bridge_rejected_candidate,
                "ClientWorldState rejected the initial observation")};
        }
        auto session = std::unique_ptr<RuntimeReplaySession>{
            new RuntimeReplaySession{
                std::move(initialization), std::move(decoder_state),
                std::move(decoder), target, 1U,
                projected.state->canonical_state_hash}};
        target = std::move(world_candidate);
        return {std::move(session), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {{}, failure(RuntimeReplayErrorCode::unable_to_retain_candidate,
            "unable to initialize the runtime replay session")};
    }
}

RuntimeReplayApplyResult RuntimeReplaySession::apply_record(
    const RuntimeReplayRecord& record)
{
    auto record_failure = [&](RuntimeReplayError error) {
        error.record_identity = record.record_identity;
        error.record_ordinal = record.record_ordinal;
        return RuntimeReplayApplyResult{std::nullopt, std::move(error)};
    };
    if (status_ != RuntimeReplaySessionStatus::active) {
        return record_failure(failure(RuntimeReplayErrorCode::session_finished,
            "runtime replay session is already finished"));
    }
    if (!target_ || !target_->runtime_observation() ||
        target_->runtime_publication_revision() != publication_revision_ ||
        target_->runtime_observation()->generation !=
            initialization_.generation ||
        target_->runtime_observation()->canonical_state_hash !=
            committed_state_hash_) {
        return record_failure(failure(
            RuntimeReplayErrorCode::client_world_state_changed,
            "ClientWorldState no longer matches the session commit point"));
    }
    if (!valid_profile(record.profile)) {
        return record_failure(failure(RuntimeReplayErrorCode::invalid_profile,
            "replay record profile is unsupported"));
    }
    if (record.generation != initialization_.generation) {
        return record_failure(failure(RuntimeReplayErrorCode::generation_mismatch,
            "replay record belongs to another generation"));
    }
    if (record.record_identity == 0U) {
        return record_failure(failure(
            RuntimeReplayErrorCode::invalid_record_identity,
            "replay record identity must be nonzero"));
    }
    if (record.record_ordinal == 0U) {
        return record_failure(failure(
            RuntimeReplayErrorCode::invalid_record_ordinal,
            "replay record ordinal must be nonzero"));
    }
    const auto fingerprint = record_hash(record);
    const auto prior = std::find_if(
        record_fingerprints_.begin(), record_fingerprints_.end(),
        [&](const RecordFingerprint& value) {
            return value.identity == record.record_identity;
        });
    if (prior != record_fingerprints_.end()) {
        return record_failure(failure(
            prior->hash == fingerprint ? RuntimeReplayErrorCode::duplicate_record
                                       : RuntimeReplayErrorCode::conflicting_record,
            prior->hash == fingerprint
                ? "record identity and content were already committed"
                : "record identity was reused with different content"));
    }
    if (record.record_ordinal <= last_record_ordinal_) {
        return record_failure(failure(RuntimeReplayErrorCode::old_record,
            "record ordinal is not newer than the committed replay order"));
    }
    if (record_fingerprints_.size() >=
        initialization_.limits.maximum_record_fingerprints) {
        return record_failure(failure(
            RuntimeReplayErrorCode::record_identity_limit_exceeded,
            "retained replay record identities reached the configured bound"));
    }
    if (publication_revision_ == (std::numeric_limits<std::uint64_t>::max)()) {
        return record_failure(failure(
            RuntimeReplayErrorCode::publication_revision_overflow,
            "runtime publication revision would overflow"));
    }

    try {
        auto staged_decoder = decoder_state_;
        const auto decoded = decoder_.decode_and_apply(
            PacketEntityDecodeInput{
                record.payload,
                record.initial_cursor,
                record.generation,
                record.record_ordinal,
                initialization_.schema_bindings.ordinary_entity,
                initialization_.schema_bindings.player_entity,
                initialization_.schema_bindings.custom_entity,
                initialization_.schema_bindings.client_data,
                initialization_.schema_bindings.weapon_data,
                ClientDataReceiverMode::ordinary_game_client,
                initialization_.user_message_definitions},
            staged_decoder);
        if (!decoded || !decoded.batch) {
            auto error = failure(
                RuntimeReplayErrorCode::decoder_failed,
                decoded.error ? decoded.error->context
                              : "A/B/C/D dispatcher rejected the record",
                decoded.error ? recovery_for(*decoded.error)
                              : RuntimeReplayRecoveryStatus::none);
            if (decoded.error) {
                error.decoder_error = decoded.error->code;
                error.clientdata_error = decoded.error->clientdata_error;
                error.delta_error = decoded.error->delta_error;
                error.decoder_cursor = decoded.error->cursor;
                error.decoder_wire_opcode = decoded.error->wire_opcode;
            }
            return record_failure(std::move(error));
        }
        const auto projection = project_candidate(
            staged_decoder, *initialization_.schemas, &record,
            &*decoded.batch, target_->runtime_observation().get(),
            publication_revision_ + 1U, initialization_.limits);
        if (projection.error || !projection.state) {
            return record_failure(projection.error
                    ? std::move(*projection.error)
                    : failure(RuntimeReplayErrorCode::bridge_rejected_candidate,
                        "runtime semantic projection failed"));
        }
        auto staged_fingerprints = record_fingerprints_;
        staged_fingerprints.push_back({record.record_identity, fingerprint});
        client::ClientWorldState staged_world = *target_;
        if (!staged_world.publish_runtime_observation(projection.state)) {
            return record_failure(failure(
                RuntimeReplayErrorCode::bridge_rejected_candidate,
                "ClientWorldState rejected the staged runtime observation"));
        }

        bool observed_time = false;
        bool observed_client = false;
        bool observed_entities = false;
        for (const auto& event : decoded.batch->events) {
            if (const auto* control = std::get_if<RuntimeControlEvent>(&event)) {
                observed_time = observed_time ||
                    control->kind == RuntimeControlMessageKind::server_time;
            } else if (std::holds_alternative<ClientDataMessageEvent>(event)) {
                observed_client = true;
            } else if (std::holds_alternative<PacketEntityMessageEvent>(event)) {
                observed_entities = true;
            }
        }

        // These move assignments are the single commit boundary. All work
        // that can reject or allocate has completed above.
        decoder_state_ = std::move(staged_decoder);
        *target_ = std::move(staged_world);
        record_fingerprints_ = std::move(staged_fingerprints);
        last_record_ordinal_ = record.record_ordinal;
        ++publication_revision_;
        committed_state_hash_ = projection.state->canonical_state_hash;
        RuntimeReplayApplyEvent event{
            record.record_identity,
            record.record_ordinal,
            record.payload.source_sequence,
            observed_time,
            observed_client,
            observed_entities,
            publication_revision_,
            projection.state->canonical_state_hash,
            std::move(*decoded.batch)};
        return {std::move(event), std::nullopt};
    } catch (const std::bad_alloc&) {
        return record_failure(failure(
            RuntimeReplayErrorCode::unable_to_retain_candidate,
            "unable to retain the staged replay transaction"));
    }
}

std::optional<RuntimeReplayError> RuntimeReplaySession::reset_generation(
    RuntimeReplayInitialization initialization)
{
    if (status_ != RuntimeReplaySessionStatus::active) {
        return failure(RuntimeReplayErrorCode::session_finished,
            "runtime replay session is already finished");
    }
    if (!target_ || !target_->runtime_observation() ||
        target_->runtime_publication_revision() != publication_revision_ ||
        target_->runtime_observation()->canonical_state_hash !=
            committed_state_hash_) {
        return failure(RuntimeReplayErrorCode::client_world_state_changed,
            "ClientWorldState no longer matches the session commit point");
    }
    if (!valid_initialization(initialization) ||
        initialization.generation == initialization_.generation) {
        return failure(RuntimeReplayErrorCode::invalid_configuration,
            "generation reset requires a different valid generation");
    }
    if (publication_revision_ == (std::numeric_limits<std::uint64_t>::max)()) {
        return failure(RuntimeReplayErrorCode::publication_revision_overflow,
            "runtime publication revision would overflow");
    }
    try {
        PacketEntitySnapshotState staged_decoder{
            initialization.generation, initialization.max_clients,
            initialization.schemas, initialization.baselines,
            initialization.limits.decoder.snapshots,
            initialization.limits.decoder.client_data};
        GoldSrcPacketEntityDecoder staged_parser{initialization.limits.decoder};
        if (!staged_decoder.valid() || !staged_parser.valid_configuration()) {
            return failure(RuntimeReplayErrorCode::invalid_configuration,
                "A/B/C/D state rejected generation initialization");
        }
        const auto projection = project_candidate(
            staged_decoder, *initialization.schemas, nullptr, nullptr, nullptr,
            publication_revision_ + 1U, initialization.limits);
        if (projection.error || !projection.state) {
            return projection.error ? std::move(projection.error)
                                    : std::optional<RuntimeReplayError>{failure(
                                          RuntimeReplayErrorCode::
                                              bridge_rejected_candidate,
                                          "generation projection failed")};
        }
        client::ClientWorldState staged_world = *target_;
        if (!staged_world.publish_runtime_observation(projection.state)) {
            return failure(RuntimeReplayErrorCode::bridge_rejected_candidate,
                "ClientWorldState rejected generation reset");
        }
        decoder_state_ = std::move(staged_decoder);
        decoder_ = std::move(staged_parser);
        initialization_ = std::move(initialization);
        *target_ = std::move(staged_world);
        record_fingerprints_.clear();
        last_record_ordinal_ = 0U;
        ++publication_revision_;
        committed_state_hash_ = projection.state->canonical_state_hash;
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return failure(RuntimeReplayErrorCode::unable_to_retain_candidate,
            "unable to stage generation reset");
    }
}

void RuntimeReplaySession::finish() noexcept
{
    status_ = RuntimeReplaySessionStatus::finished;
}

RuntimeReplaySessionStatus RuntimeReplaySession::status() const noexcept
{
    return status_;
}

std::uint64_t RuntimeReplaySession::generation() const noexcept
{
    return initialization_.generation;
}

std::uint64_t RuntimeReplaySession::publication_revision() const noexcept
{
    return publication_revision_;
}

const PacketEntitySnapshotState& RuntimeReplaySession::decoder_state()
    const noexcept
{
    return decoder_state_;
}

const client::ClientWorldState& RuntimeReplaySession::read_committed_state()
    const noexcept
{
    return *target_;
}

std::string_view to_string(const RuntimeReplayCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case RuntimeReplayCompatibilityProfile::public_goldsrc48_runtime_replay_v1:
        return "public_goldsrc48_runtime_replay_v1";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplaySpecificationSource source) noexcept
{
    switch (source) {
    case RuntimeReplaySpecificationSource::public_protocol_reference:
        return "public_protocol_reference";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplayStockVerification verification) noexcept
{
    switch (verification) {
    case RuntimeReplayStockVerification::not_verified_against_stock_runtime_payload:
        return "not_verified_against_stock_runtime_payload";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplayRecoveryStatus recovery) noexcept
{
    switch (recovery) {
    case RuntimeReplayRecoveryStatus::none: return "none";
    case RuntimeReplayRecoveryStatus::entity_full_snapshot_required:
        return "entity_full_snapshot_required";
    case RuntimeReplayRecoveryStatus::clientdata_no_base_required:
        return "clientdata_no_base_required";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplayErrorCode code) noexcept
{
    switch (code) {
    case RuntimeReplayErrorCode::invalid_configuration: return "invalid_configuration";
    case RuntimeReplayErrorCode::invalid_profile: return "invalid_profile";
    case RuntimeReplayErrorCode::target_already_has_runtime_observation: return "target_already_has_runtime_observation";
    case RuntimeReplayErrorCode::session_finished: return "session_finished";
    case RuntimeReplayErrorCode::generation_mismatch: return "generation_mismatch";
    case RuntimeReplayErrorCode::invalid_record_identity: return "invalid_record_identity";
    case RuntimeReplayErrorCode::invalid_record_ordinal: return "invalid_record_ordinal";
    case RuntimeReplayErrorCode::old_record: return "old_record";
    case RuntimeReplayErrorCode::duplicate_record: return "duplicate_record";
    case RuntimeReplayErrorCode::conflicting_record: return "conflicting_record";
    case RuntimeReplayErrorCode::record_identity_limit_exceeded: return "record_identity_limit_exceeded";
    case RuntimeReplayErrorCode::client_world_state_changed: return "client_world_state_changed";
    case RuntimeReplayErrorCode::decoder_failed: return "decoder_failed";
    case RuntimeReplayErrorCode::semantic_schema_mismatch: return "semantic_schema_mismatch";
    case RuntimeReplayErrorCode::semantic_value_mismatch: return "semantic_value_mismatch";
    case RuntimeReplayErrorCode::non_finite_semantic_value: return "non_finite_semantic_value";
    case RuntimeReplayErrorCode::projection_limit_exceeded: return "projection_limit_exceeded";
    case RuntimeReplayErrorCode::bridge_rejected_candidate: return "bridge_rejected_candidate";
    case RuntimeReplayErrorCode::publication_revision_overflow: return "publication_revision_overflow";
    case RuntimeReplayErrorCode::unable_to_retain_candidate: return "unable_to_retain_candidate";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
